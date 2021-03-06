/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license in the file COPYING
 * or http://www.opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file COPYING.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2015 Saso Kiselkov. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <cairo.h>
#include <ctype.h>
#include <xlocale.h>
#include <png.h>

#include "helpers.h"
#include "airac.h"
#include "route.h"
#include "htbl.h"
#include "wmm.h"
#include "perf.h"

#define	IMGH		1200
#define	IMGW		1200
#define	FONTSZ		14
#define	REFLON		0
#define	CHANNELS	4
#define	BPP		8
#define	FORMAT		PNG_COLOR_TYPE_RGBA
#define	SET_PIXEL(x, y, a, r, g, b) \
	do { \
		if ((x) >= 0 && (x) < IMGW && (y) >= 0 && (y) < IMGH) { \
			rows[(y)][CHANNELS * (x) + (CHANNELS - 4)] = (a); \
			rows[(y)][CHANNELS * (x) + (CHANNELS - 3)] = (r); \
			rows[(y)][CHANNELS * (x) + (CHANNELS - 2)] = (g); \
			rows[(y)][CHANNELS * (x) + (CHANNELS - 1)] = (b); \
		} \
	} while (0)

static void
prep_png_img(uint8_t *img, png_bytepp png_rows, size_t w, size_t h)
{
	for (size_t r = 0; r < h; r++) {
		png_rows[r] = &img[(r * w) * CHANNELS];
		for (size_t c = 0; c < w; c++) {
			img[(r * w + c) * CHANNELS + 3] = 0xff;
		}
	}
}

static void
xlate_png_byteorder(uint8_t *imgp, size_t w, size_t h)
{
	uint32_t *img = (uint32_t *)imgp;

	for (size_t i = 0; i < w * h; i++) {
		/* assumes little endian */
		img[i] = (img[i] & 0xff000000u) |
		    ((img[i] & 0x00ff0000) >> 16) |
		    (img[i] & 0x0000ff00) |
		    ((img[i] & 0x000000ff) << 16);
	}
}

static void
write_png_img(const char *filename, const png_bytepp rows, size_t w, size_t h)
{
	png_structp	png_ptr;
	png_infop	info_ptr;
	FILE *fp = fopen(filename, "wb");

	VERIFY(fp != NULL);
	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING,
	    NULL, NULL, NULL);
	VERIFY(png_ptr != NULL);
	info_ptr = png_create_info_struct(png_ptr);
	VERIFY(info_ptr != NULL);

	VERIFY(setjmp(png_jmpbuf(png_ptr)) == 0);
	png_init_io(png_ptr, fp);
	png_set_IHDR(png_ptr, info_ptr, w, h, BPP, PNG_COLOR_TYPE_RGBA,
	    PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
	    PNG_FILTER_TYPE_DEFAULT);
	png_write_info(png_ptr, info_ptr);
	png_write_image(png_ptr, rows);
	png_write_end(png_ptr, NULL);
	fclose(fp);
	png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1);
	png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
}

void
test_arpts(const char *navdata_dir, const char *dump,
    const waypoint_db_t *wptdb, const navaid_db_t *navdb)
{
	char		*line = NULL;
	size_t		linecap = 0;
	ssize_t		line_len;
	char		*arpt_fname;
	FILE		*arpt_fp;

	if (strlen(dump) == 4) {
		airport_t *arpt = airport_open(dump, navdata_dir, wptdb, navdb);
		if (!arpt)
			exit(EXIT_FAILURE);
		char *desc = airport_dump(arpt);
		fputs(desc, stdout);
		free(desc);
		airport_close(arpt);
		return;
	} else if (strlen(dump) > 0)
		return;

	arpt_fname = malloc(strlen(navdata_dir) + 1 +
	    strlen("Airports.txt") + 1);
	sprintf(arpt_fname, "%s" PATHSEP "%s", navdata_dir, "Airports.txt");
	arpt_fp = fopen(arpt_fname, "r");
	if (arpt_fp == NULL) {
		fprintf(stderr, "Can't open %s: %s\n", arpt_fname,
		    strerror(errno));
		exit(EXIT_FAILURE);
	}
	while ((line_len = getline(&line, &linecap, arpt_fp)) != -1) {
		char		*comps[10];
		airport_t	*arpt;

		if (explode_line(line, ',', comps, 10) != 10 ||
		    strcmp(comps[0], "A") != 0)
			continue;

		arpt = airport_open(comps[1], navdata_dir, wptdb, navdb);
		if (arpt)
			airport_close(arpt);
	}
	free(arpt_fname);
	fclose(arpt_fp);
}

void
test_airac(const char *navdata_dir, const char *dump)
{
	airway_db_t	*awydb;
	waypoint_db_t	*wptdb;
	navaid_db_t	*navdb;

	navdb = navaid_db_open(navdata_dir);
	wptdb = waypoint_db_open(navdata_dir);
	if (!wptdb)
		exit(EXIT_FAILURE);
	awydb = airway_db_open(navdata_dir, htbl_count(&wptdb->by_name));
	if (!navdb || !awydb)
		exit(EXIT_FAILURE);

	test_arpts(navdata_dir, dump, wptdb, navdb);
	if (strcmp(dump, "wpt") == 0) {
		char *desc = waypoint_db_dump(wptdb);
		fputs(desc, stdout);
		free(desc);
	}
	if (strcmp(dump, "awyname") == 0) {
		char *desc = airway_db_dump(awydb, B_TRUE);
		fputs(desc, stdout);
		free(desc);
	} else if (strcmp(dump, "awyfix") == 0) {
		char *desc = airway_db_dump(awydb, B_FALSE);
		fputs(desc, stdout);
		free(desc);
	}
	if (strcmp(dump, "navaid") == 0) {
		char *desc = navaid_db_dump(navdb);
		fputs(desc, stdout);
		free(desc);
	}

	airway_db_close(awydb);
	waypoint_db_close(wptdb);
	navaid_db_close(navdb);
}

vect2_t
test_fpp_xy(double lat, double lon, const fpp_t *fpp, double scale)
{
	vect2_t pos = geo2fpp(GEO_POS2(lat, lon), fpp);
	pos.x = pos.x * ((IMGW - 1) / (2.0 * scale)) + IMGW / 2;
	pos.y = IMGH - (pos.y * ((IMGH - 1) / (2.0 * scale)) + IMGH / 2);
	return (pos);
}

void
test_fpp(void)
{
	fpp_t			fpp;
	png_bytep		rows[IMGH];
	uint8_t			*img;
	cairo_surface_t		*surface;
	cairo_t			*cr;
	char			text[64];
	cairo_text_extents_t	ext;

#define	LAT0	0
#define	LAT1	90
#define	LATi	5
#define	LON0	-90
#define	LON1	90
#define	LONi	5
#define	SCALE	1

#define	PROJLAT	25.0
#define	PROJLON	45.0
#define	PROJROT	65.0

	fpp = ortho_fpp_init(GEO_POS2(PROJLAT, PROJLON), PROJROT, &wgs84,
	    B_TRUE);

	surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, IMGW, IMGH);
	cr = cairo_create(surface);
	img = cairo_image_surface_get_data(surface);

	prep_png_img(img, rows, IMGW, IMGH);

	cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_set_line_width(cr, 2);
	for (int lat = LAT0; lat < LAT1; lat += LATi) {
		vect2_t p = test_fpp_xy(lat, LON0, &fpp, SCALE * EARTH_MSL);
		if (IS_NULL_VECT(p))
			continue;
		cairo_move_to(cr, p.x, p.y);
		for (int lon = LON0 + LONi; lon <= LON1; lon += LONi) {
			p = test_fpp_xy(lat, lon, &fpp, SCALE * EARTH_MSL);
			if (IS_NULL_VECT(p))
				continue;
			cairo_line_to(cr, p.x, p.y);
		}
		cairo_stroke(cr);
	}
	for (int lon = LON0; lon <= LON1; lon += LONi) {
		vect2_t p = test_fpp_xy(LAT1, lon, &fpp, SCALE * EARTH_MSL);
		if (IS_NULL_VECT(p))
			continue;
		cairo_move_to(cr, p.x, p.y);
		for (int lat = LAT1 - LATi; lat >= LAT0; lat -= LATi) {
			p = test_fpp_xy(lat, lon, &fpp, SCALE * EARTH_MSL);
			if (IS_NULL_VECT(p))
				continue;
			cairo_line_to(cr, p.x, p.y);
		}
		cairo_stroke(cr);
	}

	snprintf(text, sizeof (text), "lat: %.1lf lon: %.1lf rot: %.1lf",
	    PROJLAT, PROJLON, PROJROT);
	cairo_set_font_size(cr, FONTSZ);

	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_move_to(cr, IMGW / 2 + FONTSZ / 2, IMGH / 2 - FONTSZ / 2);
	cairo_text_extents(cr, text, &ext);
	cairo_line_to(cr, IMGW / 2 + 1.5 * FONTSZ + ext.width,
	    IMGH / 2 - FONTSZ / 2);
	cairo_line_to(cr, IMGW / 2 + 1.5 * FONTSZ + ext.width,
	    IMGH / 2 - 1.5 * FONTSZ - ext.height);
	cairo_line_to(cr, IMGW / 2 + FONTSZ / 2,
	    IMGH / 2 - 1.5 * FONTSZ - ext.height);
	cairo_close_path(cr);
	cairo_fill(cr);

	cairo_set_source_rgb(cr, 1, 0, 0);
	cairo_move_to(cr, IMGW / 2 + FONTSZ, IMGH / 2 - FONTSZ);
	cairo_show_text(cr, text);

	cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
	cairo_set_source_rgb(cr, 1, 0, 0);
	cairo_set_line_width(cr, 1);
	cairo_move_to(cr, 0, IMGH / 2);
	cairo_line_to(cr, IMGW, IMGH / 2);
	cairo_stroke(cr);
	cairo_move_to(cr, IMGW / 2, 0);
	cairo_line_to(cr, IMGW / 2, IMGH);
	cairo_stroke(cr);

	xlate_png_byteorder(img, IMGW, IMGH);
	write_png_img("/tmp/test.png", rows, IMGW, IMGH);

	cairo_destroy(cr);

#define	PRINT_VECT(v) \
	printf(#v "  x: %lf\ty: %lf\tz: %lf\n", v.x, v.y, v.z)
#define	PRINT_POS(p) \
	printf(#p "  lat: %lf\tlon: %lf\telev: %.0lf\n", p.lat, p.lon, p.elev)

	vect3_t ecef = geo2ecef(GEO_POS3(PROJLAT, PROJLON, 0), &wgs84);
	geo_pos3_t pos2 = ecef2geo(ecef, &wgs84);
	sph_xlate_t xlate, inv_xlate;
	xlate = sph_xlate_init(GEO_POS2(PROJLAT, PROJLON), PROJROT, B_FALSE);
	inv_xlate = sph_xlate_init(GEO_POS2(PROJLAT, PROJLON), PROJROT, B_TRUE);

	vect3_t xlated = sph_xlate_vect(ecef, &xlate);
	vect3_t unxlated = sph_xlate_vect(xlated, &inv_xlate);
	PRINT_POS(pos2);

	PRINT_VECT(ecef);
	PRINT_VECT(xlated);
	PRINT_VECT(unxlated);
}

void
test_lcc(double reflat, double stdpar1, double stdpar2)
{
	uint8_t		*img;
	png_bytep	rows[IMGH];
	double		scale_factor, offset;

	lcc_t	lcc = lcc_init(reflat, REFLON, stdpar1, stdpar2);
	vect2_t	tip;

	img = calloc(IMGW * IMGH * CHANNELS, 1);
	prep_png_img(img, rows, IMGW, IMGH);

	{
		geo_pos2_t	min_lat = {-40, 0};
		geo_pos2_t	max_lat = {89.999999, 0};
		vect2_t		res_min = geo2lcc(min_lat, &lcc);
		vect2_t		res_max = geo2lcc(max_lat, &lcc);

		printf("min: %lf max: %lf\n", res_min.y, res_max.y);
		scale_factor = IMGH / (res_max.y - res_min.y) / 2;
		offset = res_min.y * scale_factor;
		printf("offset: %lf   scale_factor: %lf\n", offset,
		    scale_factor);
		tip = res_max;
	}

	for (double lat = -80; lat < 90.0;) {
		geo_pos2_t	maxw_pos = {lat, -90.0};
		vect2_t		maxw = geo2lcc(maxw_pos, &lcc);
		for (double lon = -160.0; lon <= 160.0;) {
			geo_pos2_t	pos = {lat, lon};
			vect2_t		res = geo2lcc(pos, &lcc);
			int		x, y;
			double scale_factor_x =
			    (res.y - tip.y) / (maxw.x - tip.x);

			x = (res.x * scale_factor_x * scale_factor) + IMGW / 2;

			y = IMGH - ((res.y * scale_factor - offset) +
			    (1 - cos(DEG2RAD(lon))) * (tip.y - res.y) *
			    scale_factor);

			if (lat < 88.0)
				lon += 2.0;
			else if (lat < 89.5)
				lon += 5.0;
			else
				lon += 20.0;
			if (lat == reflat || lat + 1 == reflat) {
				for (int i = -1; i <= 1; i++) {
					for (int j = -1; j <= 1; j++) {
						SET_PIXEL(x + i, y + j, 255,
						    255, 0, 0);
					}
				}
			} else if (lat == stdpar1 || lat == stdpar2) {
				SET_PIXEL(x, y, 255, 0, 255, 0);
			} else {
				SET_PIXEL(x, y, 255, 255, 255, 255);
			}
		}
		if (lat < 88.0)
			lat += 2.0;
		else if (lat < 89.5)
			lat += 0.5;
		else
			lat += 0.1;
	}

	xlate_png_byteorder(img, IMGW, IMGH);
	write_png_img("/tmp/test.png", rows, IMGW, IMGH);

	free(img);
}

void
test_gc(void)
{
	printf("hdg: %lf\n", gc_point_hdg(GEO_POS2(0, 0), GEO_POS2(45, 90),
	    80));
}

void
test_sph_xlate(void)
{
	sph_xlate_t	xlate = sph_xlate_init(GEO_POS2(0, 90), 0, B_FALSE);
	geo_pos2_t	pos = sph_xlate(GEO_POS2(0, 0), &xlate);
	printf("pos.lat: %lf  pos.lon: %lf\n", pos.lat, pos.lon);
}

static void
dump_route(route_t *route)
{
	const airport_t *dep, *arr, *altn1, *altn2;
	const runway_t *dep_rwy;
	const navproc_t *sid, *sidtr, *star, *startr, *appr, *apprtr;

	dep = route_get_dep_arpt(route);
	arr = route_get_arr_arpt(route);
	altn1 = route_get_altn1_arpt(route);
	altn2 = route_get_altn2_arpt(route);

	dep_rwy = route_get_dep_rwy(route);
	sid = route_get_sid(route);
	sidtr = route_get_sidtr(route);

	star = route_get_star(route);
	startr = route_get_startr(route);

	appr = route_get_appr(route);
	apprtr = route_get_apprtr(route);

	printf(
	    " ORIGIN                  DEST\n"
	    "%4s                   %4s\n"
	    " ALTN1                  ALTN2\n"
	    "%4s                   %4s\n"
	    " RUNWAY\n"
	    "%s\n",
	    dep ? dep->icao : "", arr ? arr->icao : "",
	    altn1 ? altn1->icao : "", altn2 ? altn2->icao : "",
	    dep_rwy ? dep_rwy->ID : "");

	printf(
	    " SID: %-6s   TRANS: %-6s\n"
	    "STAR: %-6s   TRANS: %-6s\n"
	    "APPR: %-6s   TRANS: %-6s\n",
	    sid ? sid->name : "", sidtr ? sidtr->tr_name : "",
	    star ? star->name : "", startr ? startr->tr_name : "",
	    appr ? appr->name : "", apprtr ? apprtr->tr_name : "");
}

static void
strtolower(char *str)
{
	while (*str) {
		*str = tolower(*str);
		str++;
	}
}

void
dump_route_leg_group(const route_leg_group_t *rlg, int idx)
{
#define	END_FIX_NAME(f) \
	(*(f)->name != 0 ? (f)->name : "VECTORS")
	switch (rlg->type) {
	case ROUTE_LEG_GROUP_TYPE_AIRWAY:
		printf("%3d %s\t\t%7s\n", idx,
		    rlg->awy->name, !IS_NULL_WPT(&rlg->end_wpt) ?
		    rlg->end_wpt.name : "");
		break;
	case ROUTE_LEG_GROUP_TYPE_DIRECT:
		printf("%3d DIRECT\t\t%7s\n", idx,
		    rlg->end_wpt.name);
		break;
	case ROUTE_LEG_GROUP_TYPE_PROC:
		switch (rlg->proc->type) {
		case NAVPROC_TYPE_SID:
			printf("%3d %s.%s\t\t%7s\n", idx,
			    rlg->proc->rwy->ID, rlg->proc->name,
			    END_FIX_NAME(&rlg->end_wpt));
			break;
		case NAVPROC_TYPE_STAR:
			printf("%3d %s\t\t%7s\n", idx,
			    rlg->proc->name, END_FIX_NAME(&rlg->end_wpt));
			break;
		case NAVPROC_TYPE_FINAL:
			printf("%3d %s  \t\t%7s\n", idx,
			    rlg->proc->name, END_FIX_NAME(&rlg->end_wpt));
			break;
		case NAVPROC_TYPE_SID_COMMON:
			printf("%3d %s.ALL\t\t%7s\n", idx,
			    rlg->proc->name, END_FIX_NAME(&rlg->end_wpt));
			break;
		case NAVPROC_TYPE_STAR_COMMON:
			printf("%3d ALL.%s\t\t%7s\n", idx,
			    rlg->proc->name, END_FIX_NAME(&rlg->end_wpt));
			break;
		case NAVPROC_TYPE_SID_TRANS:
			printf("%3d %s.%s\t%7s\n", idx,
			    rlg->proc->name, rlg->proc->tr_name,
			    END_FIX_NAME(&rlg->end_wpt));
			break;
		case NAVPROC_TYPE_STAR_TRANS:
			printf("%3d %s.%s\t%7s\n", idx,
			    rlg->proc->tr_name, rlg->proc->name,
			    END_FIX_NAME(&rlg->end_wpt));
			break;
		case NAVPROC_TYPE_FINAL_TRANS:
			printf("%3d %s.%s\t\t%7s\n", idx,
			    rlg->proc->tr_name, rlg->proc->name,
			    END_FIX_NAME(&rlg->end_wpt));
			break;
		default:
			assert(0);
		}
		break;
	case ROUTE_LEG_GROUP_TYPE_DISCO:
		printf("%3d --- ROUTE DISCONTINUITY ---\n", idx);
		break;
	default:
		break;
	}
}

static void
dump_route_leg_groups(const route_t *route)
{
	const list_t *leg_groups = route_get_leg_groups(route);
	int i = 0;

	printf("### VIA\t\t\t%7s\n", "TO");
	for (const route_leg_group_t *rlg = list_head(leg_groups); rlg;
	    rlg = list_next(leg_groups, rlg)) {
		dump_route_leg_group(rlg, i);
		i++;
	}
}

static void
dump_route_legs(const route_t *route)
{
	int i = 0;
	const list_t *leg_groups = route_get_leg_groups(route);

	for (const route_leg_group_t *rlg = list_head(leg_groups); rlg != NULL;
	    rlg = list_next(leg_groups, rlg)) {
		dump_route_leg_group(rlg, -1);
		for (const route_leg_t *rl = list_head(&rlg->legs); rl;
		    rl = list_next(&rlg->legs, rl)) {
			if (!rl->disco) {
				char *desc = navproc_seg_get_descr(&rl->seg);
				printf("%3d%s", i, desc);
				free(desc);
			} else {
				printf("%3d    [###################]\n", i);
			}
			i++;
		}
	}
}

const route_leg_group_t *
find_rlg(route_t *route, int idx)
{
	const list_t *leg_groups = route_get_leg_groups(route);
	int i = 0;
	const route_leg_group_t *res = NULL;

	if (idx < 0)
		return (NULL);
	for (res = list_head(leg_groups), i = 0; res && i < idx;
	    res = list_next(leg_groups, res), i++)
		;
	return (res);
}

const route_leg_t *
find_rl(route_t *route, int idx)
{
	const list_t *legs = route_get_legs(route);
	int i = 0;
	const route_leg_t *res = NULL;

	if (idx < 0)
		return (NULL);
	for (res = list_head(legs), i = 0; res && i < idx;
	    res = list_next(legs, res), i++)
		;
	return (res);
}

wpt_t
find_fix(const char *fix_name, fms_t *fms)
{
	wpt_t fix;
	wpt_t *fixes;
	size_t num_fixes;
	bool_t is_wpt_seq;

	fixes = fms_wpt_name_decode(fix_name, fms, &num_fixes, &is_wpt_seq);
	if (fixes == NULL) {
		fprintf(stderr, "%s\n", err2str(ERR_NOT_IN_DATABASE));
		return (null_wpt);
	}
	if (is_wpt_seq) {
		free(fixes);
		fprintf(stderr, "%s\n", err2str(ERR_INVALID_ENTRY));
		return (null_wpt);
	}

	if (num_fixes > 1) {
		unsigned idx;
		printf("  %s is ambiguous, choose one:\n", fix_name);
		for (unsigned i = 0; i < num_fixes; i++)
			printf("   %d: %s  %lf  %lf\n", i, fixes[i].name,
			    fixes[i].pos.lat, fixes[i].pos.lon);
		scanf("%u", &idx);
		ASSERT(idx < num_fixes);
		fix = fixes[idx];
	} else {
		fix = fixes[0];
	}
	free(fixes);

	return (fix);
}

static void
strtoupper(char *str)
{
	while (*str) {
		*str = toupper(*str);
		str++;
	}
}

void
test_route(char *navdata_dir)
{
	char			cmd[64];
	route_t			*route = NULL;
	fms_t			*fms;
	fms_navdb_t		*navdb;
	const acft_perf_t	*acft;
	flt_perf_t		*flt;

	fms = fms_new(navdata_dir, "doc/WMM.COF", "doc/perf_sample.csv");
	if (!fms)
		exit(EXIT_FAILURE);
	navdb = fms->navdb;
	acft = fms_acft_perf(fms);
	flt = fms_flt_perf(fms);

	route = route_create(navdb);

	while (!feof(stdin)) {
		err_t err = ERR_OK;
		route_leg_t *err_rl = NULL;

		if (scanf("%63s", cmd) != 1)
			continue;
		strtolower(cmd);
		if (strcmp(cmd, "exit") == 0) {
			break;
		}
#define	SET_RTE_PARAM_CMD(cmdname, func) \
	do { \
		if (strcmp(cmd, cmdname) == 0) { \
			char	param[8]; \
			if (scanf("%7s", param) != 1) \
				continue; \
			strtoupper(param); \
			if (strcmp(param, "NULL") == 0) \
				err = func(route, NULL); \
			else \
				err = func(route, param); \
		} \
	} while (0)
		SET_RTE_PARAM_CMD("origin", route_set_dep_arpt);
		SET_RTE_PARAM_CMD("dest", route_set_arr_arpt);
		SET_RTE_PARAM_CMD("altn1", route_set_altn1_arpt);
		SET_RTE_PARAM_CMD("altn2", route_set_altn2_arpt);
		SET_RTE_PARAM_CMD("rwy", route_set_dep_rwy);
		SET_RTE_PARAM_CMD("sid", route_set_sid);
		SET_RTE_PARAM_CMD("sidtr", route_set_sidtr);
		SET_RTE_PARAM_CMD("star", route_set_star);
		SET_RTE_PARAM_CMD("startr", route_set_startr);
		SET_RTE_PARAM_CMD("appr", route_set_appr);
		SET_RTE_PARAM_CMD("apprtr", route_set_apprtr);

		if (strcmp(cmd, "p") == 0) {
			dump_route(route);
		} else if (strcmp(cmd, "via") == 0) {
			int idx;
			char awy_name[NAV_NAME_LEN];
			const route_leg_group_t *prev_rlg;

			memset(awy_name, 0, sizeof (awy_name));
			if (scanf("%7d %7s", &idx, awy_name) != 2)
				continue;
			prev_rlg = find_rlg(route, idx - 1);
			strtoupper(awy_name);
			err = route_lg_awy_insert(route, awy_name, prev_rlg,
			    NULL);
		} else if (strcmp(cmd, "to") == 0) {
			int idx;
			char fix_name[32];
			const route_leg_group_t *rlg;

			memset(fix_name, 0, sizeof (fix_name));
			if (scanf("%7d %31s", &idx, fix_name) != 2)
				continue;
			strtoupper(fix_name);
			rlg = find_rlg(route, idx);
			if (!rlg)
				continue;
			err = route_lg_awy_set_end_fix(route, rlg, fix_name);
		} else if (strcmp(cmd, "dir") == 0) {
			int idx;
			char fix_name[32];
			const route_leg_group_t *prev_rlg;
			wpt_t fix;

			memset(fix_name, 0, sizeof (fix_name));
			if (scanf("%7d %31s", &idx, fix_name) != 2)
				continue;
			strtoupper(fix_name);
			fix = find_fix(fix_name, fms);
			if (IS_NULL_WPT(&fix))
				continue;
			prev_rlg = find_rlg(route, idx - 1);
			err = route_lg_direct_insert(route, &fix, prev_rlg,
			    NULL);
		} else if (strcmp(cmd, "ldir") == 0) {
			int idx;
			char fix_name[32];
			const route_leg_t *prev_rl;
			wpt_t fix;

			memset(fix_name, 0, sizeof (fix_name));
			if (scanf("%7d %31s", &idx, fix_name) != 2)
				continue;
			strtoupper(fix_name);
			fix = find_fix(fix_name, fms);
			if (IS_NULL_WPT(&fix))
				continue;
			prev_rl = find_rl(route, idx - 1);
			err = route_l_insert(route, &fix, prev_rl, NULL);
		} else if (strcmp(cmd, "rm") == 0) {
			int idx;
			const route_leg_group_t *rlg;
			if (scanf("%7d", &idx) != 1)
				continue;
			rlg = find_rlg(route, idx);
			if (!rlg)
				continue;
			err = route_lg_delete(route, rlg);
		} else if (strcmp(cmd, "lrm") == 0) {
			int idx;
			const route_leg_t *rl;
			if (scanf("%7d", &idx) != 1)
				continue;
			rl = find_rl(route, idx);
			if (!rl)
				continue;
			route_l_delete(route, rl);
		} else if (strcmp(cmd, "lmv") == 0) {
			int idx1, idx2;
			const route_leg_t *rl1, *rl2;
			if (scanf("%7d %7d", &idx1, &idx2) != 2)
				continue;
			rl1 = find_rl(route, idx1);
			rl2 = find_rl(route, idx2);
			if (!rl1 || !rl2) {
				fprintf(stderr, "idx out of rng\n");
				continue;
			}
			route_l_move(route, rl1, rl2);
		} else if (strcmp(cmd, "r") == 0) {
			dump_route_leg_groups(route);
		} else if (strcmp(cmd, "l") == 0) {
			dump_route_legs(route);
		} else if (strcmp(cmd, "airac") == 0) {
			char mon1[5], mon2[5];
			locale_t loc;
			struct tm tm_start, tm_end;

			loc = newlocale(LC_TIME_MASK, NULL, LC_GLOBAL_LOCALE);
			VERIFY(loc != NULL);
			gmtime_r(&navdb->valid_from, &tm_start);
			gmtime_r(&navdb->valid_to, &tm_end);
			strftime_l(mon1, 5, "%b", &tm_start, loc);
			strftime_l(mon2, 5, "%b", &tm_end, loc);
			strtoupper(mon1);
			strtoupper(mon2);
			printf("CYCLE:%u "
			    "VALID:%02d%s%02d/%02d%s%02d (%sCURRENT) "
			    "UNIX:%lu/%lu\n",
			    navdb->airac_cycle,
			    tm_start.tm_mday, mon1, tm_start.tm_year % 100,
			    tm_end.tm_mday, mon2, tm_end.tm_year % 100,
			    navdb_is_current(navdb) ? "" : "NOT ",
			    navdb->valid_from, navdb->valid_to);
			freelocale(loc);
		}

		if (err == ERR_OK)
			err = route_update(route, acft, flt, &err_rl);

		if (err != ERR_OK) {
			fprintf(stderr, "%s\n", err2str(err));
			continue;
		}
	}

	route_destroy(route);
	fms_destroy(fms);
}

void
test_magvar_run(const int npos, const char **names, const double *expct_var,
    const geo_pos3_t *pos, const double year)
{
	wmm_t *wmm;

	printf("year: %.2lf\n", year);
	wmm = wmm_open("doc/WMM.COF", year);
	ASSERT(wmm != NULL);

	for (int i = 0; i < npos; i++) {
		double res = wmm_true2mag(wmm, 0, pos[i]);

		printf("%s(%.5lfx%.5lfx%.0lf) 0 true = %.2lf mag "
		    "(expct: %.2lf): %s\n", names[i], pos[i].lat, pos[i].lon,
		    pos[i].elev, res, expct_var[i],
		    round(res * 100) == round(expct_var[i] * 100) ? "OK" :
		    "FAIL");
	}

	wmm_close(wmm);
}

void
test_magvar(void)
{
#define	NPOS		7
#define NTESTPOS	6
	char *names[NPOS] = {
		"PHOG", "YMML", "EGLL", "BGSF", "PABR", "SCCI", "FALE"
	};
	double expct_var[NPOS] = {
		11, 11, -1, -32, 19, 13, -24
	};
	geo_pos3_t pos[NPOS] = {
		{ 20.89866 , -156.4305, 54 },
		{ -37.67333, 144.84333,  434 },
		{ 51.4775, 0.46133, 83 },
		{ 67.017, -50.68933, 165 },
		{ 71.28483, -156.7685, 48 },
		{ -53.00366, -70.85366, 139 },
		{ -29.61183, 31.11933, 304 }
	};
	time_t now;
	struct tm now_d;

	char *names_test[NTESTPOS] = {
		"test1", "test2", "test3", "test4", "test5", "test6"
	};
	double expct_var_test_2015[NTESTPOS] = {
		-3.85, 0.57, 69.81, -4.27, 0.56, 69.22
	};
	double expct_var_test_2017_5[NTESTPOS] = {
		-2.75, 0.32, 69.58, -3.17, 0.32, 69.00
	};
	geo_pos3_t pos_test[NTESTPOS] = {
		{ 80, 0, 0 },
		{ 0, 120, 0 },
		{ -80, 240, 0 },
		{ 80, 0, 328084 },
		{ 0, 120, 328084 },
		{ -80, 240, 328084 }
	};

	now = time(NULL);
	localtime_r(&now, &now_d);
	test_magvar_run(NPOS, (const char **)names, expct_var, pos,
	    1900 + now_d.tm_year + (now_d.tm_mon / 12.0));
	test_magvar_run(NTESTPOS, (const char **)names_test,
	    expct_var_test_2015, pos_test, 2015);
	test_magvar_run(NTESTPOS, (const char **)names_test,
	    expct_var_test_2017_5, pos_test, 2017.5);
}

void
test_perf(const char *navdata_dir)
{
	double			ktas = 500;
	double			oat = -50;
	double			alt = 35000;
	double			qnh = 105000;
	const acft_perf_t	*acft;
	flt_perf_t		*flt;
	fms_t			*fms;

	fms = fms_new(navdata_dir, "doc/WMM.COF", "doc/perf_sample.csv");
	if (fms == NULL)
		exit(EXIT_FAILURE);
	acft = fms_acft_perf(fms);
	flt = fms_flt_perf(fms);

	printf("INPUTS:\n"
	    "  KTAS:\t\t%6.0lf KT\n"
	    "  OAT:\t\t%6.1lf deg C\n"
	    "  QNH:\t\t%6.1lf hPa\n"
	    "  ALT:\t\t%6.0lf ft\n"
	    "-------------------------\n", ktas, oat, qnh / 100, alt);

	double press = alt2press(alt, qnh);

	printf("alt2press:\t%6.1lf hPa\n"
	    "press2alt:\t%6.0lf ft\n", press / 100, press2alt(press, qnh));

	double fl = alt2fl(alt, qnh);
	printf("alt2fl:\t\t%6.0lf FL\n"
	    "fl2alt:\t\t%6.0lf ft\n", fl, fl2alt(fl, qnh));

	double mach = ktas2mach(ktas, oat);
	double a0 = speed_sound(oat), dens = air_density(press, oat),
	    Pd = dyn_press(ktas, press, oat), Pi = impact_press(mach, press);

	printf("speed of sound:\t%6.1lf m/s\n"
	    "air density:\t%6.3lf kg/m^3\n"
	    "impact press:\t%6.1lf hPa\n"
	    "dynamic press:\t%6.1lf hPa\n", a0, dens, Pi / 100, Pd / 100);

	double isadev = sat2isadev(fl, oat);
	printf("sat2isadev:\t%6.1lf deg C\n"
	    "isadev2sat:\t%6.1lf deg C\n", isadev, isadev2sat(fl, isadev));

	printf("ktas2mach:\t%6.3lf\n"
	    "mach2ktas:\t%6.1lf KT\n", mach, mach2ktas(mach, oat));

	double kcas = ktas2kcas(ktas, press, oat);
	printf("ktas2kcas:\t%6.1lf KT\n"
	    "kcas2ktas:\t%6.1lf KT\n", kcas, kcas2ktas(kcas, press, oat));

	double keas = mach2keas(mach, press);
	printf("mach2keas:\t%6.1lf KT\n"
	    "keas2mach:\t%6.3lf\n", keas, keas2mach(keas, press));

	double tat = sat2tat(oat, mach);
	printf("sat2tat:\t%6.2lf deg C\n"
	    "tat2sat:\t%6.2lf deg C\n", tat, tat2sat(tat, mach));

#define	ISADEV		0
#define	QNH		101325
#define	TP_ALT		45000
#define	MACH_LIM	0.77

	double alts[] = {
		0,	1000,	2000,	10000,	35000
	};
	double spds[] = {
		0,	165,	210,	250,	270
	};
	double flaps[] = {
		0.25,	0.1,	0,	0
	};
	double fuel, dist;
	const char *names[] = {
		"T/O",	"ACCEL",	"CLB",	"CLB"
	};
	accelclb_t type[] = {
		ACCEL_TAKEOFF, ACCEL_AND_CLB, ACCEL_THEN_CLB, ACCEL_THEN_CLB
	};

	fuel = 45000;
	dist = 0;

	for (int i = 0; i + 1 < 5; i++) {
		double dist_i, burn;

		dist_i = accelclb2dist(flt, acft, ISADEV, QNH, TP_ALT, fuel,
		    VECT2(1, 0), alts[i], spds[i], ZERO_VECT2, alts[i + 1],
		    spds[i + 1], ZERO_VECT2, flaps[i], MACH_LIM, type[i],
		    &burn);
		dist += dist_i;
		fuel -= burn;
		printf("%s:\ts: %.2lf km  s_i: %.2lf km  F: %.0lf kg"
		    "  burn_i: %.0lf kg\n", names[i], NM2MET(dist) / 1000,
		    NM2MET(dist_i) / 1000, fuel, burn);
	}

	fuel = 45000;
	double dists[] = { 10, 20, 25, 100 };
	double alts2[] = { 0, 5000, 15000, 25000, 35000 };
	double kcas2[] = { 0, 210, 290, 290, 290 };
	accelclb_t types[] = {
		ACCEL_TAKEOFF, ACCEL_THEN_CLB, ACCEL_THEN_CLB, ACCEL_THEN_CLB
	};
	dist = 0;
	for (int i = 0; i < 4; i++) {
		double dist_i = 0, burn = 0;

		dist_i = dist2accelclb(flt, acft, ISADEV, QNH, TP_ALT, fuel,
		    VECT2(1, 0), i == 0 ? flt->to_flap : 0, &alts2[i],
		    &kcas2[i], ZERO_VECT2, alts2[i + 1], kcas2[i + 1], 0.77,
		    dists[i], types[i], &burn);
		dist += dist_i;
		printf("s: %.2lf km  s_i: %.2lf km\n", NM2MET(dist) / 1000,
		    NM2MET(dist_i) / 1000);
		alts2[i + 1] = alts2[i];
		kcas2[i + 1] = kcas2[i];
	}

	fms_destroy(fms);
}

void
test_math(void)
{
	bezier_t		bez;
	vect2_t			pts[5];
	png_bytep		rows[IMGH];
	uint8_t			*img;
	cairo_surface_t		*surface;
	cairo_t			*cr;
	char			buf[32];
	cairo_text_extents_t	ext;

	surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, IMGW, IMGH);
	cr = cairo_create(surface);
	img = cairo_image_surface_get_data(surface);

	prep_png_img(img, rows, IMGW, IMGH);

#define	BEZ_PT1	VECT2(0, 0)
#define	BEZ_PT2	VECT2(1, 3.5)
#define	BEZ_PT3	VECT2(2, 2)
#define	BEZ_PT4	VECT2(3, 0.5)
#define	BEZ_PT5	VECT2(4, 4)

	pts[0] = BEZ_PT1;
	pts[1] = BEZ_PT2;
	pts[2] = BEZ_PT3;
	pts[3] = BEZ_PT4;
	pts[4] = BEZ_PT5;

	bez.n_pts = 5;
	bez.pts = pts;


#define	OFFSET		100
#define	FACT		250
#define	CAIRO_X(x)	((x * FACT) + OFFSET)
#define	CAIRO_Y(y)	(IMGH - ((y * FACT) + OFFSET))
#define	NUMPTS		20
#define	LIM(x)		(x + (x / NUMPTS / 2))

	cairo_set_font_size(cr, FONTSZ);

	cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
	cairo_set_line_width(cr, 1);
	cairo_set_source_rgb(cr, 1, 1, 1);

	for (double y = 0.0; y <= 4.0; y += 0.25) {
		cairo_move_to(cr, CAIRO_X(0), CAIRO_Y(y));
		cairo_line_to(cr, CAIRO_X(4), CAIRO_Y(y));
		cairo_stroke(cr);

		snprintf(buf, sizeof (buf), "y=%.2lf", y);
		cairo_text_extents(cr, buf, &ext);
		cairo_move_to(cr, CAIRO_X(0) - ext.width - 10, CAIRO_Y(y) +
		    ext.height / 2);
		cairo_show_text(cr, buf);
	}

	cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
	cairo_set_line_width(cr, 2);
	cairo_set_source_rgb(cr, 1, 1, 1);
	for (double x = 0.0; x < LIM(4.0); x += (4.0 / NUMPTS)) {
		cairo_move_to(cr, CAIRO_X(x),
		    CAIRO_Y(quad_bezier_func(x, &bez)));
		cairo_line_to(cr, CAIRO_X(x), CAIRO_Y(((x - pts[0].x) /
		    (pts[2].x - pts[0].x) * (pts[2].y - pts[0].y) + pts[0].y)));
		cairo_stroke(cr);

		snprintf(buf, sizeof (buf), "x=%.1lf", x);
		cairo_text_extents(cr, buf, &ext);
		cairo_move_to(cr, CAIRO_X(x) - ext.width / 2, CAIRO_Y(0) +
		    2 * ext.height);
		cairo_show_text(cr, buf);
	}
	cairo_stroke(cr);

	cairo_move_to(cr, CAIRO_X(pts[0].x), CAIRO_Y(pts[0].y));
	for (int i = 0; i < 5; i++)
		cairo_line_to(cr, CAIRO_X(pts[i].x), CAIRO_Y(pts[i].y));
	for (int i = 4; i >= 0; i -= 2)
		cairo_line_to(cr, CAIRO_X(pts[i].x), CAIRO_Y(pts[i].y));
	cairo_stroke(cr);

	cairo_set_line_width(cr, 4);

	cairo_set_source_rgb(cr, 1, 0, 0);
	for (double x = 0.0; x < LIM(4.0); x += (4.0 / NUMPTS)) {
		double y = quad_bezier_func(x, &bez);
		if (x == 0.0)
			cairo_move_to(cr, CAIRO_X(pts[0].x), CAIRO_Y(pts[0].y));
		else
			cairo_line_to(cr, CAIRO_X(x), CAIRO_Y(y));
	}
	cairo_stroke(cr);

#define	SRCH_Y	2.13
#define	LINELEN	20
	double *xs;
	size_t n_xs;
	xs = quad_bezier_func_inv(SRCH_Y, &bez, &n_xs);
	cairo_set_source_rgb(cr, 0, 1, 0);
	if (n_xs == SIZE_MAX)
		n_xs = 0;
	for (unsigned i = 0; i < n_xs; i++) {
		cairo_show_text(cr, buf);
		cairo_move_to(cr, CAIRO_X(xs[i]) - LINELEN, CAIRO_Y(SRCH_Y));
		cairo_line_to(cr, CAIRO_X(xs[i]) + LINELEN, CAIRO_Y(SRCH_Y));
		cairo_stroke(cr);
		cairo_move_to(cr, CAIRO_X(xs[i]), CAIRO_Y(SRCH_Y) - LINELEN);
		cairo_line_to(cr, CAIRO_X(xs[i]), CAIRO_Y(SRCH_Y) + LINELEN);
		cairo_stroke(cr);
	}

#undef	OFFSET
#undef	FACT
#undef	CAIRO_X
#undef	CAIRO_Y
#undef	NUMPTS
#undef	LIM

	xlate_png_byteorder(img, IMGW, IMGH);
	write_png_img("/tmp/test.png", rows, IMGW, IMGH);

	cairo_destroy(cr);
}

#define	DRAW_ROUTE	1
#define	DRAW_SEG_GUIDES	1

#define	BASELAT		0
#define	BASELON		0
#define	P1		(GEO_POS2(BASELAT - 0.08, BASELON - 0.20))
#define	P2		(GEO_POS2(BASELAT - 0.08, BASELON + 0.20))
#define	P3		(GEO_POS2(BASELAT + 0.05, BASELON + 0.20))
#define	P4		(GEO_POS2(BASELAT + 0.22, BASELON - 0.08))
#define	P5		(GEO_POS2(BASELAT + 0.10, BASELON - 0.10))
#define	P6		(GEO_POS2(BASELAT + 0.25, BASELON - 0.10))
#define	P7		(GEO_POS2(BASELAT + 0.25, BASELON + 0.05))
#define	P8		(GEO_POS2(BASELAT + 0.24, BASELON + 0.10))
#define	P9		(GEO_POS2(BASELAT + 0.23, BASELON + 0.15))
#define	P10		(GEO_POS2(BASELAT + 0.25, BASELON + 0.20))
#define	SPD1		300
#define	SPD2		250
#define	SPD3		250
#define	CW1		B_TRUE
#define	CW2		B_TRUE
#define	RNP		NM2MET(.3)
#define	STD_TURN_RATE	3

#define	PROJLAT_OFF	0.06
#define	PROJLON_OFF	0

#define	HOFFSET		600
#define	VOFFSET		400
#define	FACT		0.02
#define	CAIRO_X(x)	((x * FACT) + HOFFSET)
#define	CAIRO_Y(y)	(IMGH - ((y * FACT) + VOFFSET))
#define	STAR_SZ		(VECT2(50, 50))
#define	CROSS_SZ	(VECT2(20, 20))
#define	CAIRO_VECT2(v)	(VECT2(CAIRO_X((v).x), CAIRO_Y((v).y)))

static void
draw_star_outline(cairo_t *cr, vect2_t pos, vect2_t sz)
{
	vect2_t stem_sz = VECT2(sz.x / 5, sz.y / 5);
	cairo_move_to(cr, pos.x - stem_sz.x / 2, pos.y - stem_sz.y / 2);
	cairo_line_to(cr, pos.x - sz.x / 2, pos.y);
	cairo_line_to(cr, pos.x - stem_sz.x / 2, pos.y + stem_sz.y / 2);
	cairo_line_to(cr, pos.x, pos.y + sz.y / 2);
	cairo_line_to(cr, pos.x + stem_sz.x / 2, pos.y + stem_sz.y / 2);
	cairo_line_to(cr, pos.x + sz.x / 2, pos.y);
	cairo_line_to(cr, pos.x + stem_sz.x / 2, pos.y - stem_sz.y / 2);
	cairo_line_to(cr, pos.x, pos.y - sz.y / 2);
	cairo_close_path(cr);
}

static void
draw_star(cairo_t *cr, vect2_t pos, vect2_t sz)
{
	cairo_set_source_rgb(cr, 0, 0, 0);
	draw_star_outline(cr, pos, sz);
	cairo_fill(cr);
	cairo_set_source_rgb(cr, 1, 1, 1);
	draw_star_outline(cr, pos, sz);
	cairo_stroke(cr);
}

#if	DRAW_SEG_GUIDES && DRAW_ROUTE
static void
draw_cross(cairo_t *cr, vect2_t pos, vect2_t sz)
{
	cairo_set_source_rgb(cr, 0.67, 0.67, 0.67);
	cairo_move_to(cr, pos.x - sz.x / 2, pos.y);
	cairo_line_to(cr, pos.x + sz.x / 2, pos.y);
	cairo_stroke(cr);
	cairo_move_to(cr, pos.x, pos.y - sz.y / 2);
	cairo_line_to(cr, pos.x, pos.y + sz.y / 2);
	cairo_stroke(cr);
	cairo_set_source_rgb(cr, 1, 1, 1);
}
#endif	/* DRAW_SEG_GUIDES && DRAW_ROUTE */

#if	DRAW_SEG_GUIDES
static void
draw_seg(cairo_t *cr, route_seg_t *rs, const fpp_t *fpp)
{
	if (rs->type == ROUTE_SEG_TYPE_DIRECT) {
		vect2_t end = geo2fpp(GEO3_TO_GEO2(rs->direct.end), fpp);
		cairo_line_to(cr, CAIRO_X(end.x), CAIRO_Y(end.y));
	} else {
		vect2_t start, center, end;
		double hdg1, hdg2, angle1, angle2;

		start = geo2fpp(GEO3_TO_GEO2(rs->arc.start), fpp);
		center = geo2fpp(rs->arc.center, fpp);
		end = geo2fpp(GEO3_TO_GEO2(rs->arc.end), fpp);
		hdg1 = dir2hdg(vect2_sub(start, center));
		hdg2 = dir2hdg(vect2_sub(end, center));
		angle1 = DEG2RAD(hdg1) - M_PI / 2;
		angle2 = DEG2RAD(hdg2) - M_PI / 2;
		if (!rs->arc.cw) {
			double tmp = angle1;
			angle1 = angle2;
			angle2 = tmp;
		}

		cairo_stroke(cr);
		cairo_arc(cr, CAIRO_X(center.x), CAIRO_Y(center.y),
		    vect2_abs(vect2_sub(start, center)) * FACT,
		    angle1, angle2);
		cairo_stroke(cr);
		cairo_move_to(cr, CAIRO_X(end.x), CAIRO_Y(end.y));
	}
}
#endif	/* DRAW_SEG_GUIDES */

void
test_route_seg(void)
{
	list_t			seglist;
	route_seg_t		*rs1, *rs2, *rs3, *rs4, *rs5, *rs6, *rs7;
	png_bytep		rows[IMGH];
	uint8_t			*img;
	cairo_surface_t		*surface;
	cairo_t			*cr;
	fpp_t			fpp;
	vect2_t			pos;
#if	DRAW_ROUTE && DRAW_SEG_GUIDES
	double			dashes[2] = {5, 5};
#endif

	rs1 = calloc(sizeof (*rs1), 1);
	rs1->type = ROUTE_SEG_TYPE_DIRECT;
	rs1->direct.start = P1;
	rs1->direct.end = P2;
	rs1->join_type = ROUTE_SEG_JOIN_TRACK;

	rs2 = calloc(sizeof (*rs2), 1);
	rs2->type = ROUTE_SEG_TYPE_DIRECT;
	rs2->direct.start = P2;
	rs2->direct.end = P3;
	rs2->join_type = ROUTE_SEG_JOIN_TRACK;

	rs3 = calloc(sizeof (*rs3), 1);
	rs3->type = ROUTE_SEG_TYPE_DIRECT;
	rs3->direct.start = P3;
	rs3->direct.end = P4;
	rs3->join_type = ROUTE_SEG_JOIN_DIRECT;

	rs4 = calloc(sizeof (*rs4), 1);
	rs4->type = ROUTE_SEG_TYPE_DIRECT;
	rs4->direct.start = P4;
	rs4->direct.end = P5;
	rs4->join_type = ROUTE_SEG_JOIN_TRACK;

	rs5 = calloc(sizeof (*rs5), 1);
	rs5->type = ROUTE_SEG_TYPE_ARC;
	rs5->arc.start = P5;
	rs5->arc.end = P7;
	rs5->arc.center = P6;
	rs5->arc.cw = CW1;
	rs5->join_type = ROUTE_SEG_JOIN_TRACK;

	rs6 = calloc(sizeof (*rs6), 1);
	rs6->type = ROUTE_SEG_TYPE_ARC;
	rs6->arc.start = P7;
	rs6->arc.center = P8;
	rs6->arc.end = P9;
	rs6->arc.cw = CW2;
	rs6->join_type = ROUTE_SEG_JOIN_TRACK;

	rs7 = calloc(sizeof (*rs7), 1);
	rs7->type = ROUTE_SEG_TYPE_DIRECT;
	rs7->direct.start = P9;
	rs7->direct.end = P10;
	rs7->join_type = ROUTE_SEG_JOIN_SIMPLE;

	list_create(&seglist, sizeof (route_seg_t), offsetof(route_seg_t,
	    route_segs_node));

	list_insert_tail(&seglist, rs1);
	list_insert_tail(&seglist, rs2);
	list_insert_tail(&seglist, rs3);
	list_insert_tail(&seglist, rs4);
	list_insert_tail(&seglist, rs5);
	list_insert_tail(&seglist, rs6);
	list_insert_tail(&seglist, rs7);

	surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, IMGW, IMGH);
	cr = cairo_create(surface);
	img = cairo_image_surface_get_data(surface);

	prep_png_img(img, rows, IMGW, IMGH);
	fpp = gnomo_fpp_init(GEO_POS2(BASELAT + PROJLAT_OFF,
	    BASELON + PROJLON_OFF), 0, &wgs84, B_FALSE);

#if	DRAW_SEG_GUIDES
	cairo_set_source_rgb(cr, 0.67, 0.67, 0.67);
	cairo_set_line_width(cr, 1);
	pos = geo2fpp(GEO3_TO_GEO2(((route_seg_t *)list_head(
	    &seglist))->direct.start), &fpp);
	cairo_move_to(cr, CAIRO_X(pos.x), CAIRO_Y(pos.y));
	for (route_seg_t *rs = list_head(&seglist); rs;
	    rs = list_next(&seglist, rs))
		draw_seg(cr, rs, &fpp);
	cairo_stroke(cr);
#endif

	cairo_set_line_width(cr, 3);
	cairo_set_source_rgb(cr, 1, 1, 1);

	for (route_seg_t *rs = list_head(&seglist); rs;
	    rs = list_next(&seglist, rs)) {
		route_seg_t *rs_next = list_next(&seglist, rs);
		if (rs_next != NULL)
			rs = route_seg_join(&seglist, rs, rs_next, RNP, SPD1,
			    STD_TURN_RATE);
	}

#if	DRAW_ROUTE
	for (route_seg_t *rs = list_head(&seglist); rs;
	    rs = list_next(&seglist, rs)) {
		if (rs->type == ROUTE_SEG_TYPE_DIRECT) {
			vect2_t start, end;
			start = geo2fpp(GEO3_TO_GEO2(rs->direct.start), &fpp);
			end = geo2fpp(GEO3_TO_GEO2(rs->direct.end), &fpp);

#if	DRAW_SEG_GUIDES
			cairo_set_line_width(cr, 2);
			draw_cross(cr, CAIRO_VECT2(start), CROSS_SZ);
			draw_cross(cr, CAIRO_VECT2(end), CROSS_SZ);
			cairo_set_line_width(cr, 3);
#endif	/* DRAW_SEG_GUIDES */
			cairo_move_to(cr, CAIRO_X(start.x), CAIRO_Y(start.y));
			cairo_line_to(cr, CAIRO_X(end.x), CAIRO_Y(end.y));
			cairo_stroke(cr);
		} else {
			vect2_t start, end, center;
			double hdg1, hdg2, angle1, angle2;

			start = geo2fpp(GEO3_TO_GEO2(rs->arc.start), &fpp);
			end = geo2fpp(GEO3_TO_GEO2(rs->arc.end), &fpp);
			center = geo2fpp(rs->arc.center, &fpp);
#if	DRAW_SEG_GUIDES
			cairo_set_line_width(cr, 2);
			draw_cross(cr, CAIRO_VECT2(center), CROSS_SZ);
			draw_cross(cr, CAIRO_VECT2(start), CROSS_SZ);
			draw_cross(cr, CAIRO_VECT2(end), CROSS_SZ);
			cairo_set_line_width(cr, 3);
#endif	/* DRAW_SEG_GUIDES */
			hdg1 = dir2hdg(vect2_sub(start, center));
			hdg2 = dir2hdg(vect2_sub(end, center));
			angle1 = DEG2RAD(hdg1) - M_PI / 2;
			angle2 = DEG2RAD(hdg2) - M_PI / 2;
			if (!rs->arc.cw) {
				double tmp = angle1;
				angle1 = angle2;
				angle2 = tmp;
			}

			cairo_arc(cr, CAIRO_X(center.x), CAIRO_Y(center.y),
			    vect2_abs(vect2_sub(start, center)) * FACT,
			    angle1, angle2);
			cairo_stroke(cr);

#if	DRAW_SEG_GUIDES
			cairo_set_line_width(cr, 1);
			cairo_set_dash(cr, dashes, 2, 0);
			cairo_set_source_rgb(cr, 0.67, 0.67, 0.67);
			cairo_move_to(cr, CAIRO_X(center.x), CAIRO_Y(center.y));
			cairo_line_to(cr, CAIRO_X(start.x), CAIRO_Y(start.y));
			cairo_stroke(cr);
			cairo_move_to(cr, CAIRO_X(center.x), CAIRO_Y(center.y));
			cairo_line_to(cr, CAIRO_X(end.x), CAIRO_Y(end.y));
			cairo_stroke(cr);
			cairo_set_line_width(cr, 3);
			cairo_set_source_rgb(cr, 1, 1, 1);
			cairo_set_dash(cr, NULL, 0, 0);
#endif	/* DRAW_SEG_GUIDES */
		}
	}
#endif	/* DRAW_ROUTE */

	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_set_line_width(cr, 2);
	pos = geo2fpp(GEO3_TO_GEO2(P1), &fpp);
	draw_star(cr, CAIRO_VECT2(pos), STAR_SZ);
	pos = geo2fpp(GEO3_TO_GEO2(P2), &fpp);
	draw_star(cr, CAIRO_VECT2(pos), STAR_SZ);
	pos = geo2fpp(GEO3_TO_GEO2(P3), &fpp);
	draw_star(cr, CAIRO_VECT2(pos), STAR_SZ);
	pos = geo2fpp(GEO3_TO_GEO2(P4), &fpp);
	draw_star(cr, CAIRO_VECT2(pos), STAR_SZ);
	pos = geo2fpp(GEO3_TO_GEO2(P5), &fpp);
	draw_star(cr, CAIRO_VECT2(pos), STAR_SZ);
	pos = geo2fpp(GEO3_TO_GEO2(P7), &fpp);
	draw_star(cr, CAIRO_VECT2(pos), STAR_SZ);
	pos = geo2fpp(GEO3_TO_GEO2(P9), &fpp);
	draw_star(cr, CAIRO_VECT2(pos), STAR_SZ);
	pos = geo2fpp(GEO3_TO_GEO2(P10), &fpp);
	draw_star(cr, CAIRO_VECT2(pos), STAR_SZ);

	xlate_png_byteorder(img, IMGW, IMGH);
	write_png_img("/tmp/test.png", rows, IMGW, IMGH);

	cairo_destroy(cr);
#undef	OFFSET
#undef	FACT
#undef	CAIRO_X
#undef	CAIRO_Y
}

int
main(int argc, char **argv)
{
	UNUSED(argc);
	UNUSED(argv);

#ifdef	TEST_AIRAC
	char *dump = "", *airac_dir = NULL;
	int c;
	while ((c = getopt(argc, argv, "a:d:")) != -1) {
		switch (c) {
		case 'a':
			airac_dir = optarg;
			break;
		case 'd':
			dump = optarg;
			break;
		default:
			return 1;
		}
	}
	if (airac_dir == NULL) {
		fprintf(stderr, "Missing navdata_dir argument\n");
		return (1);
	}
	test_airac(airac_dir, dump);
#endif
#ifdef	TEST_LCC
	test_lcc(40, 30, 50);
#endif
#ifdef	TEST_FPP
	test_fpp();
#endif
#ifdef	TEST_SPH_XLATE
	test_sph_xlate();
#endif
#ifdef	TEST_ROUTE
	test_route(argv[optind]);
#endif
#ifdef	TEST_MAGVAR
	test_magvar();
#endif
#ifdef	TEST_PERF
	test_perf(argv[optind]);
#endif
#ifdef	TEST_MATH
	test_math();
#endif
#ifdef	TEST_ROUTE_SEG
	test_route_seg();
#endif

	return (0);
}
