#include <u.h>
#include <libc.h>
#include <bio.h>

struct Tracker {
	uchar *data;
	char ended;
	uvlong t;
	int cmd;
} *tr;

typedef struct Tracker Tracker;

int fd, tempo = 500000, ntrack;
Biobuf *bf;
uvlong T;

char *ntab[] = {
	"c0","c♯0","d0","d♯0","e0","f0","f♯0","g0","g♯0","a0","a♯0","b0",
	"c1","c♯1","d1","d♯1","e1","f1","f♯1","g1","g♯1","a1","a♯1","b1",
	"c2","c♯2","d2","d♯2","e2","f2","f♯2","g2","g♯2","a2","a♯2","b2",
	"c3","c♯3","d3","d♯3","e3","f3","f♯3","g3","g♯3","a3","a♯3","b3",
	"c4","c♯4","d4","d♯4","e4","f4","f♯4","g4","g♯4","a4","a♯4","b4",
	"c5","c♯5","d5","d♯5","e5","f5","f♯5","g5","g♯5","a5","a♯5","b5",
	"c6","c♯6","d6","d♯6","e6","f6","f♯6","g6","g♯6","a6","a♯6","b6",
	"c7","c♯7","d7","d♯7","e7","f7","f♯7","g7","g♯7","a7","a♯7","b7",
	"c8","c♯8","d8","d♯8","e8","f8","f♯8","g8","g♯8","a8","a♯8","b8",
	"c9","c♯9","d9","d♯9","e9","f9","f♯9","g9","g♯9","a9","a♯9","b9",
	"c10","c♯10","d10","d♯10","e10","f10","f♯10","g10"
};
char nts[512], *ntp = nts;

void *
emallocz(int size)
{
	void *v;
	
	v = malloc(size);
	if(v == nil)
		sysfatal("malloc: %r");
	memset(v, 0, size);
	return v;
}

int
get8(Tracker *src)
{
	uchar c;

	if(src == nil){
		if(read(fd, &c, 1) == 0)
			sysfatal("unexpected eof");
		return c;
	}
	return *src->data++;
}

int
get16(Tracker *src)
{
	int x;
	
	x = get8(src) << 8;
	return x | get8(src);
}

int
get32(Tracker *src)
{
	int x;
	x = get16(src) << 16;
	return x | get16(src);
}

int
getvar(Tracker *src)
{
	int k, x;
	
	x = get8(src);
	k = x & 0x7F;
	while(x & 0x80){
		k <<= 7;
		x = get8(src);
		k |= x & 0x7F;
	}
	return k;
}

int
peekvar(Tracker *src)
{
	uchar *p;
	int v;
	
	p = src->data;
	v = getvar(src);
	src->data = p;
	return v;
}

void
skip(Tracker *src, int x)
{
	if(x) do
		get8(src);
	while(--x);
}

void
paste(int dt)
{
	/* FIXME: attempt to use note duration instead of clocks */
	Bprint(bf, "c%d %s\n", dt, ntp!=nts?nts:"+");
}

void
readevent(Tracker *src, int dt)
{
	int n, v, t;

	if(dt != 0){
		paste(dt);
		ntp = nts;
	}
	src->t += getvar(src);
	t = get8(src);
	if((t & 0x80) == 0){
		src->data--;
		t = src->cmd;
		if((t & 0x80) == 0)
			sysfatal("invalid midi");
	}else
		src->cmd = t;
	n = t >> 4;
	switch(n){
	case 0x8:
		n = get8(src);
		get8(src);
	off:
		ntp = seprint(ntp, nts + sizeof nts, " %d%s,0", t & 15, ntab[n]);
		break;
	case 0x9:
		n = get8(src);
		v = get8(src);
		if(v == 0)
			goto off;
		ntp = seprint(ntp, nts + sizeof nts, " %d%s", t & 15, ntab[n]);
		if(v != 64)
			ntp = seprint(ntp, nts + sizeof nts, ",%d", v);
		ntp = strecpy(ntp, nts + sizeof nts, "-");
		break;
	case 0xB:
		get16(src);
		break;
	case 0xC:
		get8(src);
		break;
	case 0xE:
		get16(src);
		break;
	case 0xF:
		if((t & 0xF) == 0){
			while(get8(src) != 0xF7)
				;
			return;
		}
		v = get8(src);
		n = get8(src);
		switch(v){
		case 0x2F:
			src->ended = 1;
			break;
		case 0x51:
			v = get16(src) << 8;
			v |= get8(src);
			if(v != tempo){
				Bprint(bf, "t %d\n", 60000000/v);
				tempo = v;
			}
			break;
		default:
			skip(src, n);
		}
		break;
	default:
		sysfatal("unknown event type %x", t>>4);
	}
}

void
main(int argc, char **argv)
{
	int i, size;
	uvlong T, t, mint, dt;
	Tracker *x, *minx;

	if(argc > 1)
		fd = open(argv[1], OREAD);
	if(fd < 0)
		sysfatal("open: %r");
	bf = Bfdopen(1, OWRITE);
	if(bf == nil)
		sysfatal("Bfdopen: %r");
	if(get32(nil) != 0x4D546864 || get32(nil) != 6)
		sysfatal("invalid file header");
	i = get16(nil);
	if(i != 0 && i != 1)
		sysfatal("unsupported midi format %d\n", i);
	ntrack = get16(nil);
	Bprint(bf, "div %d\n", get16(nil));
	tr = emallocz(ntrack * sizeof(*tr));
	for(i = 0; i < ntrack; i++){
		if(get32(nil) != 0x4D54726B)
			sysfatal("invalid track header");
		size = get32(nil);
		tr[i].data = emallocz(size);
		readn(fd, tr[i].data, size);
	}
	T = 0;
	for(;;){
		minx = nil;
		mint = 0;
		for(x = tr; x < tr + ntrack; x++){
			if(x->ended)
				continue;
			t = peekvar(x) + x->t;
			if(t < mint || minx == nil){
				mint = t;
				minx = x;
			}
		}
		if(minx == nil)
			exits(nil);
		dt = mint - T;
		readevent(minx, dt);
		T += dt;
	}
}
