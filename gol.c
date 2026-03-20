#include <SDL2/SDL.h>

struct chunk {
	int x;
	int y;
	unsigned long cells;
	bool occ;
};

struct chunk_map {
	unsigned long cap;
	unsigned long num;
	struct chunk *data;
};

uint64_t map_hash(int x, int y) {
	unsigned long s = ((unsigned long)x << 32) | (unsigned int)y;
	s += 0x9e3779b97f4a7c15ULL;
	s = (s ^ (s >> 30)) * 0xbf58476d1ce4e5b9ULL;
	s = (s ^ (s >> 27)) * 0x41f30206230f3549ULL;
	return s ^ (s >> 31);
}

void chunk_map_set(struct chunk_map *map, int x, int y, unsigned long cells) {
	if (map->num == map->cap / 2) {
		struct chunk_map new = {
			.cap = map->cap * 2,
			.num = 0,
		};
		new.data = malloc(sizeof(struct chunk) * new.cap);
		memset(new.data, 0, sizeof(struct chunk) * new.cap);
		for (unsigned long i = 0; i < map->cap; i++) {
			if (!map->data[i].occ) continue;
			struct chunk c = map->data[i];
			chunk_map_set(&new, c.x, c.y, c.cells);
		}
		free(map->data);
		*map = new;
	}
	unsigned long off = map_hash(x, y) % map->cap;
	for (unsigned long i = off;; i = (i + 1) % map->cap) {
		if (map->data[i].occ && map->data[i].x == x && map->data[i].y == y) {
			map->data[i].cells = cells;
			break;
		}
		if (map->data[i].occ) continue;
		map->data[i] = (struct chunk){
			.x = x,
				.y = y,
				.cells = cells,
				.occ = 1
		};
		map->num++;
		break;
	}
}

bool chunk_map_exists(struct chunk_map *map, int x, int y) {
	unsigned long off = map_hash(x, y) % map->cap;
	for (unsigned long i = off; map->data[i].occ; i = (i + 1) % map->cap) {
		if ((map->data[i].x != x || map->data[i].y != y)) continue;
		return 1;
	}
	return 0;
}

unsigned long chunk_map_get(struct chunk_map *map, int x, int y) {
	unsigned long off = map_hash(x, y) % map->cap;
	for (unsigned long i = off; map->data[i].occ; i = (i + 1) % map->cap) {
		if ((map->data[i].x != x || map->data[i].y != y)) continue;
		return map->data[i].cells;
	}
	return 0;
}

void chunk_map_clear(struct chunk_map *map) {
	map->num = 0;
	memset(map->data, 0, sizeof(struct chunk) * map->cap);
}

void chunk_map_init(struct chunk_map *map) {
	map->cap = 16;
	map->num = 0;
	map->data = malloc(sizeof(struct chunk_map) * 16);
	memset(map->data, 0, sizeof(struct chunk_map) * 16);
}

void render_chunks(SDL_Renderer *renderer, struct chunk_map *map, bool show_chunks, float camx, float camy, float zoom) {
	SDL_FRect rect;
	for (unsigned long i = 0; i < map->cap; i++) {
		if (!map->data[i].occ) continue;
		struct chunk chunk = map->data[i];
		rect = (SDL_FRect){
			((float)chunk.x * 8 - camx) * zoom + 400,
			((float)chunk.y * 8 - camy) * zoom + 300,
			zoom * 8, zoom * 8
		};

		if (show_chunks) {
			SDL_SetRenderDrawColor(renderer, 0, 80, 0, 255);
			SDL_RenderFillRectF(renderer, &rect);
		}
		for (int x = 0; x < 8; x++) {
			for (int y = 0; y < 8; y++) {
				if ((chunk.cells >> ((7 - y) * 8 + (7 - x)) & 1) != 1) continue;
				int gx = chunk.x * 8 + x;
				int gy = chunk.y * 8 + y;
				rect = (SDL_FRect){
					((float)gx - camx) * zoom + 400,
					((float)gy - camy) * zoom + 300,
					zoom, zoom
				};

				SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
				SDL_RenderFillRectF(renderer, &rect);
			}
		}
	}
}

#define LSMASK 0x8080808080808080
#define LBMASK 0x7f7f7f7f7f7f7f7f

#define RSMASK 0x0101010101010101
#define RBMASK 0xfefefefefefefefe

void update_chunk(struct chunk_map *cur, struct chunk_map *next, int x, int y, bool prop) {
	if (chunk_map_exists(cur, x, y) && prop) return;
	if (chunk_map_exists(next, x, y)) return;

	unsigned long cells = chunk_map_get(cur, x, y);
	unsigned long a = chunk_map_get(cur, x - 1, y - 1);
	unsigned long b = chunk_map_get(cur, x, y - 1);
	unsigned long c = chunk_map_get(cur, x + 1, y - 1);
	unsigned long d = chunk_map_get(cur, x - 1, y);
	unsigned long e = chunk_map_get(cur, x + 1, y);
	unsigned long f = chunk_map_get(cur, x - 1, y + 1);
	unsigned long g = chunk_map_get(cur, x, y + 1);
	unsigned long h = chunk_map_get(cur, x + 1, y + 1);

	unsigned long nt = (cells >> 8) | (b << 56);
	unsigned long nb = (cells << 8) | (g >> 56);
	unsigned long nl = ((cells >> 1) & LBMASK) | ((d << 7) & LSMASK);
	unsigned long nr = ((cells << 1) & RBMASK) | ((e >> 7) & RSMASK);

	unsigned long dt = (d >> 8) | (a << 56);
	unsigned long et = (e >> 8) | (c << 56);
	
	unsigned long db = (d << 8) | (f >> 56);
	unsigned long eb = (e << 8) | (h >> 56);

	unsigned long ntl = ((nt >> 1) & LBMASK) | ((dt << 7) & LSMASK);
	unsigned long ntr = ((nt << 1) & RBMASK) | ((et >> 7) & RSMASK);
	unsigned long nbl = ((nb >> 1) & LBMASK) | ((db << 7) & LSMASK);
	unsigned long nbr = ((nb << 1) & RBMASK) | ((eb >> 7) & RSMASK);

	unsigned long one = 0;
	unsigned long two = 0;
	unsigned long dead = 0;

	#define MAGIC(q) \
		dead |= q & one & two; \
		two ^= q & one; \
		one ^= q;

	MAGIC(nt);
	MAGIC(nb);
	MAGIC(nr);
	MAGIC(nl);
	MAGIC(ntl);
	MAGIC(ntr);
	MAGIC(nbl);
	MAGIC(nbr);

	unsigned long n = (one | cells) & two & ~dead;

	if (n) chunk_map_set(next, x, y, n);

	if (cells && !prop) {
		update_chunk(cur, next, x - 1, y - 1, 1);
		update_chunk(cur, next, x, y - 1, 1);
		update_chunk(cur, next, x + 1, y - 1, 1);
		update_chunk(cur, next, x - 1, y, 1);
		update_chunk(cur, next, x + 1, y, 1);
		update_chunk(cur, next, x - 1, y + 1, 1);
		update_chunk(cur, next, x, y + 1, 1);
		update_chunk(cur, next, x + 1, y + 1, 1);
	}
}

void update_chunks(struct chunk_map *cur, struct chunk_map *next) {
	chunk_map_clear(next);
	for (unsigned long i = 0; i < cur->cap; i++) {
		if (!cur->data[i].occ) continue;
		struct chunk c = cur->data[i];
		update_chunk(cur, next, c.x, c.y, 0);
	}
	struct chunk_map tmp = *cur;
	*cur = *next;
	*next = tmp;
}

int main() {
	SDL_Window *window;
	SDL_Renderer *renderer;

	float camx = 0;
	float camy = 0;
	float zoom = 4;

	struct chunk_map cur;
	struct chunk_map next;
	chunk_map_init(&cur);
	chunk_map_init(&next);
	for (int i = 0; i < 0x100; i++) {
		unsigned long s = rand();
		s <<= 32;
		s |= rand();
		chunk_map_set(&cur, (i&0xf) - 8, (i>>4) - 8, s);
	}

	if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
		fprintf(stderr, "Failed to init sdl2\n");
		return 1;
	}

	if (SDL_CreateWindowAndRenderer(800, 600, SDL_WINDOW_SHOWN, &window, &renderer) != 0) {
		fprintf(stderr, "Failed to create window\n");
		return 1;
	}

	SDL_RenderSetVSync(renderer, 1);

	bool show_chunks = 0;
	bool move = 0;
	bool play = 0;
	bool quit = 0;
	SDL_Event e;

	while (!quit) {
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT) {
				quit = true;
			}
			if (e.type == SDL_KEYDOWN) {
				if (e.key.keysym.sym == SDLK_q) {
					quit = true;
				}
				if (e.key.keysym.sym == SDLK_c) {
					show_chunks = !show_chunks;
				}
				if (e.key.keysym.sym == SDLK_s) {
					update_chunks(&cur, &next);
				}
				if (e.key.keysym.sym == SDLK_p) {
					play = !play;
				}
			}
			if (e.type == SDL_MOUSEBUTTONDOWN) {
				if (e.button.button == SDL_BUTTON_LEFT) {
					move = 1;
				}
			}
			if (e.type == SDL_MOUSEBUTTONUP) {
				if (e.button.button == SDL_BUTTON_LEFT) {
					move = 0;
				}
			}
			if (e.type == SDL_MOUSEMOTION) {
				if (move) {
					camx -= e.motion.xrel / zoom;
					camy -= e.motion.yrel / zoom;
				}
			}
			if (e.type == SDL_MOUSEWHEEL) {
				if (e.wheel.y > 0) {
					zoom *= 1.1;
				}
				if (e.wheel.y < 0) {
					zoom /= 1.1;
				}
			}
		}

		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);
		render_chunks(renderer, &cur, show_chunks, camx, camy, zoom);
		SDL_RenderPresent(renderer);
		if (play) update_chunks(&cur, &next);
	}
}

