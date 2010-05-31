#!/usr/bin/tcc -run

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char ** split(char *in, char *out[])
{
	int o = 0;
	char * p = 0;
	while ((p = strsep(&in, "\r\n\t ")) != NULL) {
		if (!*p) continue;
		out[o++] = p;
	}
	out[o++] = NULL;
	return out;
}

int main(int argc, char *argv[])
{
	FILE *f =  fopen("output.bin.txt", "r");
	char base[128], *basei[10];
	long count, timebase;
	struct edge {
		long up, down;
		float mean;
	};
	struct {
		float * f;
		uint8_t window;	// 5 samples needs to be ON to make an edge
		long edgecount;
		struct edge *edge;
	} channel[4] = {0,0,0,0};
	long index = 0, i, c;
	
	fgets(base, sizeof(base)-1, f);
	split(base, basei);
	count = atoi(basei[5]);
	timebase = atoi(basei[2]);
	printf("time %ld count %ld\n", timebase, count);

	for (i = 0; i < 2; i++) {
		channel[i].f = (float*) malloc(count * sizeof(float));
		channel[i].edge = (struct edge*)malloc((count/5) * sizeof(struct edge));
	}
	while (!feof(f)) {
		char line[128], *linei[4];

		fgets(line, sizeof(line)-1, f);
		if (line[0] == '#')
			continue;
		split(line, linei);
	//	printf("%5d %s + %s\n", index, linei[0], linei[1]);

		channel[0].f[index] = linei[0] ? atof(linei[0]) : 0;
		channel[1].f[index] = linei[1] ? atof(linei[1]) : 0;
		index++;
	}
	fclose(f);

	for (c = 0; c < 2; c++) {
		uint8_t got = 0;
		int level = -1;
		for (i = 0; i < count; i++) {
			float s = channel[c].f[i];
			int edge;
			got = (got << 1) | 1;
			channel[c].window <<= 1;
			channel[c].window |= s >= 330.0 ? 1 : 0;
			if ((got & 0x1f) != 0x1f)
				continue;
			edge = (channel[c].window & 0x1f) == 0x1f ? 1 :
				(channel[c].window & 0x1f) == 0 ? 0 : edge;
			printf("e = %d s %.1f win %02x\n", edge, s, channel[c].window);
			if (edge != level) {
				printf("%6d EDGE %d\n", i, edge);
				if (edge == 0)
					channel[c].edge[channel[c].edgecount].down = i - 5;					
				if (level == 0 && edge == 1) 
					channel[c].edgecount++;
				if (edge == 1)
					channel[c].edge[channel[c].edgecount].up = i - 5;					
				level = edge;
			}
		} 
		printf("channel %d has %d edges\n", c, channel[c].edgecount);
	}
	
}
