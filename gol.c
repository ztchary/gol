#include <SDL2/SDL.h>
#include <pthread.h>
#include <math.h>

struct chunk {
	int x;
	int y;
	uint64_t cells;
	bool occ;
};

struct chunk_map {
	uint64_t cap;
	uint64_t num;
	struct chunk *data;
};

struct runner_data {
	struct chunk_map *cur;
	struct chunk_map *next;
	bool *play;
	bool *quit;
};

struct cam {
	float x;
	float y;
	float zoom;
};

uint64_t map_hash(uint32_t x, uint32_t y) {
	uint64_t s = ((uint64_t)x << 32) | (uint32_t)y;
	s += 0x9e3779b97f4a7c15ULL;
	s = (s ^ (s >> 30)) * 0xbf58476d1ce4e5b9ULL;
	s = (s ^ (s >> 27)) * 0x41f30206230f3549ULL;
	return s ^ (s >> 31);
}

void chunk_map_set(struct chunk_map *map, int x, int y, uint64_t cells) {
	if (map->num == map->cap / 2) {
		struct chunk_map new = {
			.cap = map->cap * 2,
			.num = 0,
		};
		new.data = malloc(sizeof(struct chunk) * new.cap);
		memset(new.data, 0, sizeof(struct chunk) * new.cap);
		for (uint64_t i = 0; i < map->cap; i++) {
			if (!map->data[i].occ) continue;
			struct chunk c = map->data[i];
			chunk_map_set(&new, c.x, c.y, c.cells);
		}
		free(map->data);
		*map = new;
	}
	uint64_t off = map_hash(x, y) % map->cap;
	for (uint64_t i = off;; i = (i + 1) % map->cap) {
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
	uint64_t off = map_hash(x, y) % map->cap;
	for (uint64_t i = off; map->data[i].occ; i = (i + 1) % map->cap) {
		if ((map->data[i].x != x || map->data[i].y != y)) continue;
		return 1;
	}
	return 0;
}

uint64_t chunk_map_get(struct chunk_map *map, int x, int y) {
	uint64_t off = map_hash(x, y) % map->cap;
	for (uint64_t i = off; map->data[i].occ; i = (i + 1) % map->cap) {
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

void render_chunks(SDL_Renderer *renderer, struct chunk_map *map, bool show_chunks, struct cam cam) {
	SDL_FRect rect;
	int width, height;
	SDL_GetRendererOutputSize(renderer, &width, &height);
	for (uint64_t i = 0; i < map->cap; i++) {
		if (!map->data[i].occ) continue;
		struct chunk chunk = map->data[i];
		rect = (SDL_FRect){
			((float)chunk.x * 8 - cam.x) * cam.zoom + width/2,
			((float)chunk.y * 8 - cam.y) * cam.zoom + height/2,
			cam.zoom * 8, cam.zoom * 8
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
					((float)gx - cam.x) * cam.zoom + width/2,
					((float)gy - cam.y) * cam.zoom + height/2,
					cam.zoom, cam.zoom
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

	uint64_t cells = chunk_map_get(cur, x, y);
	uint64_t a = chunk_map_get(cur, x - 1, y - 1);
	uint64_t b = chunk_map_get(cur, x, y - 1);
	uint64_t c = chunk_map_get(cur, x + 1, y - 1);
	uint64_t d = chunk_map_get(cur, x - 1, y);
	uint64_t e = chunk_map_get(cur, x + 1, y);
	uint64_t f = chunk_map_get(cur, x - 1, y + 1);
	uint64_t g = chunk_map_get(cur, x, y + 1);
	uint64_t h = chunk_map_get(cur, x + 1, y + 1);

	uint64_t nt = (cells >> 8) | (b << 56);
	uint64_t nb = (cells << 8) | (g >> 56);
	uint64_t nl = ((cells >> 1) & LBMASK) | ((d << 7) & LSMASK);
	uint64_t nr = ((cells << 1) & RBMASK) | ((e >> 7) & RSMASK);

	uint64_t dt = (d >> 8) | (a << 56);
	uint64_t et = (e >> 8) | (c << 56);
	
	uint64_t db = (d << 8) | (f >> 56);
	uint64_t eb = (e << 8) | (h >> 56);

	uint64_t ntl = ((nt >> 1) & LBMASK) | ((dt << 7) & LSMASK);
	uint64_t ntr = ((nt << 1) & RBMASK) | ((et >> 7) & RSMASK);
	uint64_t nbl = ((nb >> 1) & LBMASK) | ((db << 7) & LSMASK);
	uint64_t nbr = ((nb << 1) & RBMASK) | ((eb >> 7) & RSMASK);

	uint64_t one = 0;
	uint64_t two = 0;
	uint64_t dead = 0;

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

	uint64_t n = (one | cells) & two & ~dead;

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
	for (uint64_t i = 0; i < cur->cap; i++) {
		if (!cur->data[i].occ) continue;
		struct chunk c = cur->data[i];
		update_chunk(cur, next, c.x, c.y, 0);
	}
	struct chunk_map tmp = *cur;
	*cur = *next;
	*next = tmp;
}

bool get_cell_cam(SDL_Renderer *renderer, struct chunk_map *map, struct cam cam, int x, int y) {
	int width, height;
	SDL_GetRendererOutputSize(renderer, &width, &height);

	int gx = (int)floorf((x - width/2.0f) / cam.zoom + cam.x);
	int gy = (int)floorf((y - height/2.0f) / cam.zoom + cam.y);
	int chunkx = gx >= 0 ? gx / 8 : (gx - 7) / 8;
	int chunky = gy >= 0 ? gy / 8 : (gy - 7) / 8;
	int cellx = gx - chunkx * 8;
	int celly = gy - chunky * 8;

	uint64_t cells = chunk_map_get(map, chunkx, chunky);
	return cells >> ((7 - celly) * 8 + (7 - cellx)) & 1UL;
}

void set_cell_cam(SDL_Renderer *renderer, struct chunk_map *map, struct cam cam, int x, int y, bool v) {
	int width, height;
	SDL_GetRendererOutputSize(renderer, &width, &height);

	int gx = (int)floorf((x - width/2.0f) / cam.zoom + cam.x);
	int gy = (int)floorf((y - height/2.0f) / cam.zoom + cam.y);
	int chunkx = gx >= 0 ? gx / 8 : (gx - 7) / 8;
	int chunky = gy >= 0 ? gy / 8 : (gy - 7) / 8;
	int cellx = gx - chunkx * 8;
	int celly = gy - chunky * 8;

	uint64_t cells = chunk_map_get(map, chunkx, chunky);
	uint64_t ch = 1UL << ((7 - celly) * 8 + (7 - cellx));
	cells = (cells & ~ch) | (ch * v);

	chunk_map_set(map, chunkx, chunky, cells);
}

void *runner(void *data) {
	struct runner_data d = *(struct runner_data *)data;

	for (;;) {
		if (*d.quit) return NULL;
		if (!*d.play) {
			SDL_Delay(10);
			continue;
		}
		update_chunks(d.cur, d.next);
	}
}

int main() {
	SDL_Window *window;
	SDL_Renderer *renderer;

	struct cam cam = {
		.x = 0,
		.y = 0,
		.zoom = 4
	};

	struct chunk_map cur;
	struct chunk_map next;
	chunk_map_init(&cur);
	chunk_map_init(&next);

	for (int i = 0; i < 0x1000; i++) {
		uint64_t s = rand();
		s <<= 32;
		s |= rand();
		chunk_map_set(&cur, (i&0x3f) - 8, (i>>6) - 8, s);
	}

	if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
		fprintf(stderr, "Failed to init sdl2\n");
		return 1;
	}

	if (SDL_CreateWindowAndRenderer(800, 600, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE, &window, &renderer) != 0) {
		fprintf(stderr, "Failed to create window\n");
		return 1;
	}

	SDL_RenderSetVSync(renderer, 1);

	bool show_chunks = 0;
	bool move = 0;
	int draw = -1;
	bool play = 0;
	bool quit = 0;
	SDL_Event e;

	struct runner_data d = {
		.cur = &cur,
		.next = &next,
		.play = &play,
		.quit = &quit
	};

	pthread_t tid;
	pthread_create(&tid, NULL, runner, (void *)&d);

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
				if (e.key.keysym.sym == SDLK_r) {
					chunk_map_clear(&cur);
				}
			}
			if (e.type == SDL_MOUSEBUTTONDOWN) {
				if (e.button.button == SDL_BUTTON_RIGHT) {
					move = 1;
				}
				if (e.button.button == SDL_BUTTON_LEFT) {
					draw = !get_cell_cam(renderer, &cur, cam, e.button.x, e.button.y);
					set_cell_cam(renderer, &cur, cam, e.button.x, e.button.y, draw);
				}
			}
			if (e.type == SDL_MOUSEBUTTONUP) {
				if (e.button.button == SDL_BUTTON_RIGHT) {
					move = 0;
				}
				if (e.button.button == SDL_BUTTON_LEFT) {
					draw = -1;
				}
			}
			if (e.type == SDL_MOUSEMOTION) {
				if (move) {
					cam.x -= e.motion.xrel / cam.zoom;
					cam.y -= e.motion.yrel / cam.zoom;
				}
				if (draw != -1) {
					set_cell_cam(renderer, &cur, cam, e.motion.x, e.motion.y, draw);
				}
			}
			if (e.type == SDL_MOUSEWHEEL) {
				if (e.wheel.y > 0) {
					cam.zoom *= 1.1;
				}
				if (e.wheel.y < 0) {
					cam.zoom /= 1.1;
				}
			}
		}

		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);
		render_chunks(renderer, &cur, show_chunks, cam);
		SDL_RenderPresent(renderer);
	}
}

