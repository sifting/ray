#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <SDL2/SDL.h>

/*video*/
static SDL_Window *window;
static int window_width;
static int window_height;
/*Resizes the window while maintaining the aspect ratio*/
void
vid_resize (int w, int h, float aspect)
{
	SDL_Rect bounds;
	int dx, dy;
	/*Get difference between new size and old*/
	dx = w - window_width;
	dx = dx < 0 ? -dx : dx;
	dy = h - window_height;
	dy = dy < 0 ? -dy : dy;
	/*Biggest differences gets used to derive size. If result can't 
	fit in the screen, then the size is derived from the bounds
	of the screenspace instead.*/
	SDL_GetDisplayBounds (0, &bounds);
	if (dx < dy)
	{
		w = aspect*h;
		if (bounds.w < w)
		{
			w = bounds.w;
			h = bounds.w/aspect;
		}
	}
	else
	{
		h = w/aspect;
		if (bounds.h < h)
		{
			w = aspect*bounds.h;
			h = bounds.h;
		}
	}
	SDL_SetWindowSize (window, w, h);
	window_width = w;
	window_height = h;
}
void 
vid_init (const char *title, int width, int height)
{
    SDL_InitSubSystem (SDL_INIT_VIDEO);
    window = SDL_CreateWindow
	(
		title,
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		width,
		height,
		SDL_WINDOW_OPENGL |
		SDL_WINDOW_RESIZABLE
    );
    if (NULL == window) 
	{
		return;
	}
	window_width = width;
	window_height = height;
}
void
vid_shutdown (void)
{
	SDL_DestroyWindow (window);
}

/*draw*/
static SDL_Renderer *renderer;
static SDL_Texture *texture;
void
draw_sys_init (void)
{
	renderer = SDL_CreateRenderer 
	(
		window, 
		-1, 
		SDL_RENDERER_ACCELERATED
	);
	if (NULL == renderer)
	{
		return;
	}
	texture = SDL_CreateTexture
	(
		renderer,
		SDL_PIXELFORMAT_RGB888,
		SDL_TEXTUREACCESS_STREAMING,
		320,
		240
	);
	if (NULL == texture)
	{
		return;
	}
}
void
draw_sys_shutdown (void)
{
	SDL_DestroyTexture (texture);
	SDL_DestroyRenderer (renderer);
}

unsigned char map[] = 
{
	1, 1, 1, 1, 1, 1, 1, 1,
	1, 0, 0, 0, 0, 1, 1, 1,
	1, 0, 0, 0, 0, 0, 1, 1,
	1, 1, 0, 0, 0, 0, 0, 1,
	1, 1, 0, 0, 0, 0, 0, 1,
	1, 0, 0, 0, 0, 0, 1, 1,
	1, 0, 0, 0, 0, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1,
};
int mw = 8;
int mh = 8;

#define ANGLE360 2048
#define ANGLE270 1536
#define ANGLE180 1024
#define ANGLE90 512
#define ANGLE45 256
#define ANGLEMASK	(ANGLE360-1)
#define FIXEDSHIFT 16
#define FIXEDONE (1<<FIXEDSHIFT)
#define FIXEDMASK	(FIXEDONE-1)
#define SCR_WIDTH 320
#define SCR_HALFWIDTH (SCR_WIDTH/2)
#define SCR_HEIGHT 240
#define SCR_HALFHEIGHT (SCR_HEIGHT/2)

typedef int Fixed;

typedef struct _Sprite
{
	Fixed tr[3];
}Sprite;

typedef struct _Dpy_sprite
{/*32 bytes*/
	Fixed depth;
	Fixed sc, ns, t;
	int index;
	short x1, x2;
	short y1, y2;
	short z;
	char pad[2];
}Dpy_sprite;

unsigned char *_images[16];


/*tentative*/
Sprite _spr[32];
int _nspr;

Dpy_sprite _dpy[32];
int _ndpy;

Fixed _zb[SCR_WIDTH];
short _fovtbl[SCR_WIDTH];

Fixed _sintbl[ANGLE360];
Fixed _tantbl[ANGLE90];

Fixed _px, _py, _pva;

Fixed _rayx;
Fixed _rayy;
Fixed _rayz;
int _rayn;

unsigned char *_pix;

void
draw_begin (void)
{
	int pt;
	_ndpy = 0;
	SDL_LockTexture (texture, NULL, (void **)&_pix, &pt);
}
void
draw_end (void)
{
	SDL_UnlockTexture (texture);
}
void
draw_present (void)
{
	SDL_RenderCopy (renderer, texture, NULL, NULL);
	SDL_RenderPresent (renderer);
}

static inline Fixed
tofixed (double x)
{
	return (Fixed)(65535.0*x);
}

unsigned char *
tga_load (const char *file)
{
#pragma pack(push, 1)
#pragma pack(1)
	typedef struct _TargaHeader 
	{
		unsigned char id_length, pal_type, type;
		unsigned short pal_index, pal_length;
		unsigned char pal_size;
		unsigned short x, y, width, height;
		unsigned char pixel_size, attributes;
	}TargaHeader;
#pragma pack(pop)
	TargaHeader h;
	unsigned char *b;
	FILE *fp; 
	
	fp = fopen (file, "rb");
	if (!fp)
	{
		return NULL;
	}
	
	fread (&h, 1, 18, fp);
	b = malloc (h.width*h.height*3);
	fseek (fp, h.id_length + h.pal_length, SEEK_CUR);
	fread (b, 1, h.width*h.height*3, fp);
	fclose (fp);
	
	return b;
}

void
draw_init (void)
{
	double angle, apc, arc;
	int i;
	vid_init ("raycaster", 320, 240);
	draw_sys_init ();
	/*Trig tables*/
	for (i = 0; i < ANGLE360; i++)
	{
		double th = i*3.14159265/ANGLE180;
		_sintbl[i] = tofixed (sin (th));
		if (i < ANGLE90)
		{
			_tantbl[i] = tofixed (tan (th) + 0.0031);
		}
	}
	/*FOV table*/
	angle = 60.0/180*3.14159265;
	apc = (angle*ANGLE180/3.14159265)/SCR_WIDTH;
	arc = 0.5;
	for (i = 0; i < SCR_WIDTH; i++)
	{
		_fovtbl[i] = (short)(170.667 - arc);
		arc += apc;
	}
	
	_images[0] = tga_load ("floor.tga");
	_images[1] = tga_load ("sky.tga");
	_images[2] = tga_load ("mu.tga");
	_images[3] = tga_load ("books.tga");
	
	_spr[0].tr[0] = tofixed (4);
	_spr[0].tr[1] = tofixed (3.5);
	_spr[0].tr[2] = 64;
	
	_spr[1].tr[0] = tofixed (4.5);
	_spr[1].tr[1] = tofixed (4.5);
	_spr[1].tr[2] = 64;
	
	_spr[2].tr[0] = tofixed (4);
	_spr[2].tr[1] = tofixed (5);
	_spr[2].tr[2] = 64;
	
	_nspr = 3;
}
void
draw_shutdown (void)
{
	draw_sys_shutdown ();
	vid_shutdown ();
}

static inline Fixed
cosx (int angle)
{
	return _sintbl[(angle + ANGLE90)&ANGLEMASK];
}
static inline Fixed
sinx (int angle)
{
	return _sintbl[angle&ANGLEMASK];
}
static inline Fixed
tanx (int angle)
{
	_tantbl[angle&ANGLEMASK];
}
static inline Fixed
absx (Fixed x)
{
	register int mask = x>>31;
	return (x + mask)^mask;
}
static inline Fixed
mulx (Fixed a, Fixed b)
{
	return (Fixed)(((long long)a*b)>>16);
}
static inline Fixed
divx (Fixed a, Fixed b)
{
	return (Fixed)(((long long)a<<16)/b);
}
static void
raycast (Fixed x, Fixed y, int angle)
{
	Fixed px, py, pz;
	Fixed h, v;
	int sx, sy;
	int gx, gy;
	int n;
	/*Setup for DDA algorithm*/
	angle &= ANGLEMASK;
	if (angle < ANGLE90)
	{
		sx = 1;
		sy = -1;
		v = -_tantbl[angle];
		h = _tantbl[ANGLE90 - 1 - angle];
		px = x + mulx (h, y&FIXEDMASK);
		py = y + mulx (v, FIXEDONE - (x&FIXEDMASK));
	}
	else if (angle < ANGLE180)
	{
		sx = -1;
		sy = -1;
		v = -_tantbl[ANGLE180 - 1 - angle];
		h = -_tantbl[angle - ANGLE90];
		px = x + mulx (h, (y&FIXEDMASK));
		py = y + mulx (v, (x&FIXEDMASK));
	}
	else if (angle < ANGLE270)
	{
		sx = -1;
		sy = 1;
		v = _tantbl[angle - ANGLE180];
		h = -_tantbl[ANGLE270 - 1 - angle];
		px = x + mulx (h, FIXEDONE - (y&FIXEDMASK));
		py = y + mulx (v, (x&FIXEDMASK));
	}
	else
	{
		sx = 1;
		sy = 1;
		v = _tantbl[ANGLE360 - 1 - angle];
		h = _tantbl[angle - ANGLE270];
		px = x + mulx (h, FIXEDONE - (y&FIXEDMASK));
		py = y + mulx (v, FIXEDONE - (x&FIXEDMASK));
	}
	gx = (x>>FIXEDSHIFT) + sx;
	gy = (y>>FIXEDSHIFT) + sy;
	/*Do DDA algorithm*/
	while (1)
	{
		int mapy = py>>FIXEDSHIFT;
		/*These conditions can be refactored out*/
		if ((sy == -1 && mapy <= gy) || (sy == 1 && mapy >= gy))
		{
			int mapx = px>>FIXEDSHIFT;
			if (map[gy*8 + mapx] == 0)
			{
				gy += sy;
				px += h;
				continue;
			}
			py = (gy + (sy == -1))<<FIXEDSHIFT;
			n = 0;
			break;
		}
		else
		{
			if (map[mapy*8 + gx] == 0)
			{
				gx += sx;
				py += v;
				continue;
			}
			px = (gx + (sx == -1))<<FIXEDSHIFT;
			n = 1;
			break; 
		}
	}
	/*Compute distance*/
	pz = absx 
	(
		mulx (cosx (angle), (px - x)) -
		mulx (sinx (angle), (py - y))
	);
	/*Store results*/
	_rayx = px;
	_rayy = py;
	_rayz = pz;
	_rayn = n;
}
static void
drawmap (void)
{
	int i, j;
	for (i = 0; i < SCR_WIDTH; i++)
	{
		Fixed z, h;
		int ws, we; 
		int angle;
		int sm;
		/*Cast ray from arc and prospective correct the z result*/
		angle = _pva + _fovtbl[i];
		raycast (_px, _py, angle);
		_zb[i] = z = mulx (cosx (_fovtbl[i]), _rayz);
		/*Calculate projected wall height, and set up draw variables*/
		h = (277<<16)/z;
		ws = SCR_HALFHEIGHT - (h>>1);
		we = ws + h;
		if (ws < 0) ws = 0;
		if (SCR_HEIGHT <= we) we = SCR_HEIGHT;
		/*Draw sky*/
		int offset = (i - (_pva>>1))&1023;
		for (j = 0; j < ws; j++)
		{
			int ndx = (j<<10)*3 + offset*3;
			_pix[j*SCR_WIDTH*4 + i*4 + 0] = _images[1][ndx + 0];
			_pix[j*SCR_WIDTH*4 + i*4 + 1] = _images[1][ndx + 1];
			_pix[j*SCR_WIDTH*4 + i*4 + 2] = _images[1][ndx + 2];
			_pix[j*SCR_WIDTH*4 + i*4 + 3] = 0xff;
		}
		/*Paint wall*/
		int slice = _rayx;
		int mask = 0xc0;
		if (_rayn)
		{
			slice = _rayy;
			mask = 0xff;
		}
		slice = ((slice>>9)&127)*3;
		for (j = ws; j < we; j++)
		{
			int t = ((j - SCR_HALFHEIGHT)<<8) + (h<<7);
			int ty = ((t<<7)/h)>>8;
			int ndx = (ty<<7)*3 + slice;
			_pix[j*SCR_WIDTH*4 + i*4 + 0] = _images[3][ndx + 0]&mask;
			_pix[j*SCR_WIDTH*4 + i*4 + 1] = _images[3][ndx + 1]&mask;
			_pix[j*SCR_WIDTH*4 + i*4 + 2] = _images[3][ndx + 2]&mask;
			_pix[j*SCR_WIDTH*4 + i*4 + 3] = 0xff;
		}
		/*Paint floor*/
		for (j = we; j < SCR_HEIGHT; j++)
		{/*Cast a ray from screen space to world space to the floor*/
			Fixed d = (277<<15)/(j - SCR_HALFHEIGHT);
			Fixed t = divx (d, cosx (_fovtbl[i]));
			Fixed x = _px + mulx (t, cosx (angle));
			Fixed y = _py - mulx (t, sinx (angle));
			/*Shift x,y into texel units*/
			x = (x>>9)&127;
			y = (y>>9)&127;
			int ndx = (y<<7)*3 + x*3;
			/*Shade floor*/
			_pix[j*SCR_WIDTH*4 + i*4 + 0] = _images[0][ndx + 0];
			_pix[j*SCR_WIDTH*4 + i*4 + 1] = _images[0][ndx + 1];
			_pix[j*SCR_WIDTH*4 + i*4 + 2] = _images[0][ndx + 2];
			_pix[j*SCR_WIDTH*4 + i*4 + 3] = 0xff;
		}
	}
}
static int
sortcmp (const void *a, const void *b)
{
	return ((Dpy_sprite *)b)->depth - ((Dpy_sprite *)a)->depth;
}
static void
emitsprites (void)
{
	Fixed c, s;
	int i;
	c = cosx (_pva);
	s = sinx (_pva);
	/*Cull list of submitted sprites to just what's visible*/
	for (i = 0; i < _nspr; i++)
	{
		Sprite *sp = &_spr[i];
		Fixed dx = sp->tr[0] - _px;
		Fixed dy = sp->tr[1] - _py;
		Fixed forward = mulx (dx, c) - mulx (dy, s);
		/*Reject sprite if behind camera*/
		if (forward < 0x2000)
		{
			continue;
		}
		Fixed right = mulx (dx, s) + mulx (dy, c);
		/*Reject if outside frustrum*/
		if (absx (right) > (forward<<2))
		{
			continue;
		}
		/*Ensure sprite is on screen at least in part*/
		Fixed size = (277<<16)/forward;
		int o = SCR_HALFWIDTH + mulx (right - (1<<14), size);
		if (o > SCR_WIDTH)
		{
			continue;
		}
		int w = SCR_HALFWIDTH + mulx (right + (1<<14), size);
		if (w < 0)
		{
			continue;
		}
		size >>= 1;
		/*Emit display sprite*/
		//int x = ((((j - o)<<8)<<6)/size)>>8;
		int hori = SCR_HALFHEIGHT + divx (sp->tr[2], forward);
		int s = hori - (size>>1);
		int e = s + size;
		Dpy_sprite *ds = &_dpy[_ndpy++];
		ds->depth = forward;
		ds->sc = size;
		ds->ns = divx (FIXEDONE, size);
		ds->index = 0;
		ds->x1 = (0 < o) ? o : 0;
		ds->x2 = (w < SCR_WIDTH) ? w : SCR_WIDTH-1;
		ds->y1 = (0 < s) ? s : 0;
		ds->y2 = (e < SCR_HEIGHT) ? e : SCR_HEIGHT-1;
		ds->t = ds->x1 < o ? 0 : ds->ns*(ds->x1 - o);
		ds->z = hori;
	}
	/*Sort list to descending z*/
	qsort (_dpy, _ndpy, sizeof (_dpy[0]), sortcmp);
}
void
drawsprites (void)
{
	int i, j, k;
	emitsprites ();
	for (i = 0; i < _ndpy; i++)
	{
		Dpy_sprite *ds = &_dpy[i];
		Fixed size = ds->sc;
		Fixed ix = ds->ns;
		int x = ds->t;
		int z = ds->z;
		for (j = ds->x1; j < ds->x2; j++)
		{
			int slice = x;
			x += ix;
			/*Reject column if Z buffer depth is closer than sprite*/
			if (_zb[j] < ds->depth)
			{
				continue;
			}
			slice = ((slice>>10)>>FIXEDSHIFT);
			slice &= 0x3f;
			for (k = ds->y1; k < ds->y2; k++)
			{
				int t = ((z - k)<<8) + (size<<7);
				int ty = (((t<<6)-1)/size)>>8;
				int ndx = (ty<<6)*3 + slice*3;
				/*Skip transparent pixels*/
				int px = (_images[2][ndx + 0]<<16) | 
						  (_images[2][ndx + 1]<<8) |
						  (_images[2][ndx + 2]);
				if (px == 0x00ff00)
				{
					continue;
				}
				/*Draw column*/
				_pix[k*SCR_WIDTH*4 + j*4 + 0] = _images[2][ndx + 0];
				_pix[k*SCR_WIDTH*4 + j*4 + 1] = _images[2][ndx + 1];
				_pix[k*SCR_WIDTH*4 + j*4 + 2] = _images[2][ndx + 2];
				_pix[k*SCR_WIDTH*4 + j*4 + 3] = 0xff;
			}
		}
	}
}


int 
main (int argc, char **argv) 
{
	SDL_Init (SDL_INIT_TIMER);
	draw_init ();
	_px = tofixed (3.5);
	_py = tofixed (3.5);
	_pva = 0;
	while (1) 
	{
		SDL_Event ev;
		while (SDL_PollEvent (&ev)) 
		{
			switch (ev.type)
			{
			case SDL_WINDOWEVENT:
				switch (ev.window.event)
				{
				case SDL_WINDOWEVENT_RESIZED:
					vid_resize (ev.window.data1, ev.window.data2, 4/3.);
					break;
				default:
					break;
				}
				break;
			case SDL_KEYDOWN:
				switch (ev.key.keysym.scancode)
				{
				case SDL_SCANCODE_W:
					_px += mulx (0x1000, cosx (_pva));
					_py -= mulx (0x1000, sinx (_pva));
					break;
				case SDL_SCANCODE_A:
					_px -= mulx (0x1000, sinx (_pva));
					_py -= mulx (0x1000, cosx (_pva));
					break;
				case SDL_SCANCODE_S:
					_px -= mulx (0x1000, cosx (_pva));
					_py += mulx (0x1000, sinx (_pva));
					break;
				case SDL_SCANCODE_D:
					_px += mulx (0x1000, sinx (_pva));
					_py += mulx (0x1000, cosx (_pva));
					break;
				case SDL_SCANCODE_Q:
					_pva += 15;
					break;
				case SDL_SCANCODE_E:
					_pva -= 15;
					break;
				}
				break;
			case SDL_QUIT:
				draw_shutdown ();
				SDL_Quit ();
				exit (0);
			default:
				break;
			}
		}
		draw_begin ();
		drawmap ();
		drawsprites ();
		draw_end ();
		draw_present ();
		
		//For aesthetic
		SDL_Delay (50);
	}
    return 0;
}
