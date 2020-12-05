#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <bio.h>

enum{
	Nmn = 60000000,
	Nbuf = 64*1024,
	Held = 1<<7
};
u32int lastdt;
int div, ch[16*128], line;
char *arg[64];
uchar *outb, *oute, *outp, *last;
uchar hdr[] = {
	'M', 'T', 'h', 'd',
	0x00, 0x00, 0x00, 0x06,
	0x00, 0x00,
	0x00, 0x01,
	0x00, 0x00,
	'M', 'T', 'r', 'k',
	0x00, 0x00, 0x00, 0x00,
	0x00
};

#define PUT16(p,v)	(p)[0]=(v)>>8;(p)[1]=(v)
#define PUT24(p,v)	(p)[0]=(v)>>16;(p)[1]=(v)>>8;(p)[2]=(v)
#define PUT32(p,v)	(p)[0]=(v)>>24;(p)[1]=(v)>>16;(p)[2]=(v)>>8;(p)[3]=(v)

int
out(void *u, int n)
{
	uintptr off;
	ulong len;

	if(outp + n >= oute){
		off = outp - outb;
		len = oute - outb + Nbuf;
		outb = realloc(outb, len);
		if(outb == nil)
			sysfatal("realloc: %r");
		outp = outb + off;
		oute = outb + len;
	}
	memmove(outp, u, n);
	outp += n;
	last = outp;
	lastdt = 0;
	return n;
}

void
barf(void)
{
	uchar eot[] = {0xff, 0x2f, 0x00};

	out(eot, sizeof eot);
	PUT16(outb+4+4+2+2, div);
	PUT32(outb+4+4+2+2+2+4, outp - outb - sizeof hdr + 1);
	write(1, outb, outp - outb);
}

void
setnt(int c, int n, int v)
{
	uchar u[4];

	u[0] = c | 0x80 | (v != 0) << 4;
	u[1] = n;
	u[2] = v;
	u[3] = 0;
	out(u, sizeof u);
	last--;
}

void
setdt(u32int dt)
{
	int n;
	u32int v;
	uchar u[4], *up;

	memset(u, 0, sizeof u);
	if(last == nil)
		last = outp;
	if(lastdt + dt < dt)
		setnt(0, 0, ch[0] & 0x7f);
	v = lastdt += dt;
	up = u + sizeof(u) - 1;
	*up-- = lastdt & 0x7f;
	while(n = lastdt >>= 7 & 0x7f)
		*up-- = 1<<7 | n;
	outp = last;
	last -= out(up+1, sizeof(u) + u - up - 1);
	lastdt = v;
}

void
setdiv(int n, char **arg)
{
	char *p;

	if(line != 1)
		sysfatal("line %d: div setting not on first line", line);
	if(n != 1)
		sysfatal("line %d: invalid argument", line);
	/* FIXME: maybe wrap strtol in a function or something (va?) */
	div = strtol(*arg, &p, 0);
	if(div <= 0)
		sysfatal("line %d: invalid div value %d", line, div);
}

void
setbpm(int n, char **arg)
{
	static uchar u[] = {0xff, 0x51, 0x03, 0x00, 0x00, 0x00, 0x00};
	int T;
	char *p;

	if(n != 1)
		sysfatal("line %d: invalid argument", line);
	n = strtol(*arg, &p, 0);
	T = 0;
	if(n <= 0 || (T = Nmn / n) <= 0)
		sysfatal("line %d: invalid bpm value %d", line, n);
	PUT24(u+3, T);
	out(u, sizeof u);
	last--;
}

void
setinst(int n, char **arg)
{
	static uchar u[] = {0xc0, 0x00, 0x00};
	char *p;

	if(n != 2)
		sysfatal("line %d; invalid argument", line);
	n = strtol(*arg, &p, 0);
	if(p == *arg || n & ~15 || n == 9)
		sysfatal("line %d: invalid channel number %d", line, n);
	u[0] |= n;
	n = strtol(*++arg, &p, 0);
	if(p == *arg || n & ~127)
		sysfatal("line %d: invalid instrument number %d", line, n);
	u[1] = n;
	out(u, sizeof u);
	last--;
}

void
parsent(char *s)
{
	static tt[] = {9, 11, 0, 2, 4, 5, 7};
	int c, n, o, v, *np;
	char *p;
	Rune r;

	c = strtol(s, &p, 10);
	if(p == s || c < 0 || c > 15)
		sysfatal("line %d: invalid channel number %d", line, c);
	n = tolower(*p++);
	if(n < 'a' || n > 'g')
		sysfatal("line %d: invalid note name", line);
	n = tt[n - 'a'];
	s = p + chartorune(&r, p);
	if(r == 'b' || r == L'♭'){
		n--;
		p = s;
	}else if(r == '#' || r == L'♯'){
		n++;
		p = s;
	}
	s = p;
	o = strtol(s, &p, 10);
	if(p == s || o < -1 || o > 9)
		sysfatal("line %d: invalid octave number", line);
	o++;
	n += 12 * o;
	if(n < 0 || n > 127)
		sysfatal("line %d: invalid note number", line);
	np = ch + c * 128 + n;
	v = 64;
	if(*p == ','){
		s = p + 1;
		v = strtol(s, &p, 10);
		if(p == s || v < 0 || v > 127)
			sysfatal("line %d: invalid velocity", line);
	}
	if(*np & 0x7f)
		setnt(c, n, 0);
	*np = v;
	if(*p == '-')
		*np |= Held;
	setnt(c, n, v);
}

u32int
parsedt(char *s)
{
	ulong d;
	u32int dt;
	char *p;

	if(*s == 'c'){
		d = strtoul(s+1, &p, 10);
		if(p == s+1)
			sysfatal("line %d: invalid note duration", line);
		return d;
	}
	d = strtoul(s, &p, 10);
	if(d == 0 || d & ~0xff || d != 1 && d & d - 1)
		sysfatal("line %d: invalid note duration", line);
	dt = div * 4;
	while(d >>= 1)
		dt >>= 1;
	if(*p == '/'){
		d = *++p - '0';
		if(d != 3 && d != 5)
			sysfatal("line %d: unsupported tuplet %c", line, (char)d);
		dt /= d;
	}
	if(*p == '.')
		dt = dt * 3 >> 1;
	return dt;
}

void
parse(int n)
{
	static int gdt;
	int *np, hold, grace;
	u32int dt;
	char c, **sp, **se;

	line++;
	if(n < 1)
		return;
	hold = grace = 0;
	for(sp=arg, se=arg+n; sp<se; sp++){
		c = **sp;
		if(c == '#' || c == '+'){
			n = sp - arg;
			hold = c == '+';
			break;
		}
	}
	if(n < 1)
		return;
	sp = arg;
	switch(**arg){
	case 'd': setdiv(n-1, arg+1); return;
	/* FIXME: grace note: g [dt] [note]...; take dt from next note's
	 * duration, is additive (multiple grace notes) and no updates occur
	 * when one is struck */
	/* FIXME: more intelligent argument parsing, wrt parsedt and parsent
	 * and arguments list */
	case 'g': grace = 1; sp++; n--; sysfatal("unimplemented"); break;
	case 'i': setinst(n-1, arg+1); return;
	case 't': setbpm(n-1, arg+1); return;
	}
	dt = parsedt(*(sp++));
	if(!hold && !grace)
		for(np=ch; np<ch+nelem(ch); np++)
			if(*np & 0x7f && (n == 1 || (*np & Held) == 0))
				setnt((np - ch) / 128, (np - ch) % 128, 0);
	while(--n > 0){
		if(*sp[0] == '#' || *sp[0] == '+')
			break;
		parsent(*(sp++));
	}
	setdt(dt);
}

void
usage(void)
{
	fprint(2, "usage: %s [-d div] [file]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	int n;
	char *s;
	Biobuf *bf;

	div = 96*5;
	ARGBEGIN{
	default: usage();
	case 'd': div = strtol(EARGF(usage()), nil, 0); break;
	}ARGEND
	bf = *argv != nil ? Bopen(*argv, OREAD) : Bfdopen(0, OREAD);
	if(bf == nil)
		sysfatal("init: %r");
	outb = mallocz(Nbuf, 1);
	if(outb == nil)
		sysfatal("mallocz: %r");
	oute = outb + Nbuf;
	memcpy(outb, hdr, sizeof hdr);
	outp = outb + sizeof hdr;
	setbpm(1, (arg[0] = "120", arg));	/* FIXME: >:( */
	for(;;){
		s = Brdstr(bf, '\n', 1);
		if(s == nil)
			break;
		n = getfields(s, arg, nelem(arg), 1, " \t");
		parse(n);
		free(s);
	}
	Bterm(bf);
	barf();
	exits(nil);
}
