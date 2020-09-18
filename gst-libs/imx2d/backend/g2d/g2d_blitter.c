#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <g2d.h>
#include "imx2d/imx2d_priv.h"
#include "g2d_blitter.h"


/* Disabled YVYU, since there is a bug in G2D - G2D_YUYV and G2D_YVYU
 * actually refer to the same pixel format (G2D_YUYV)
 *
 * Disabled NV16, since the formats are corrupted, and it appears
 * to be a problem with G2D itself
 */
static Imx2dPixelFormat const supported_source_pixel_formats[] =
{
	IMX_2D_PIXEL_FORMAT_RGB565,
	IMX_2D_PIXEL_FORMAT_BGR565,
	IMX_2D_PIXEL_FORMAT_RGBX8888,
	IMX_2D_PIXEL_FORMAT_RGBA8888,
	IMX_2D_PIXEL_FORMAT_BGRX8888,
	IMX_2D_PIXEL_FORMAT_BGRA8888,
	IMX_2D_PIXEL_FORMAT_XRGB8888,
	IMX_2D_PIXEL_FORMAT_ARGB8888,
	IMX_2D_PIXEL_FORMAT_XBGR8888,
	IMX_2D_PIXEL_FORMAT_ABGR8888,

	IMX_2D_PIXEL_FORMAT_PACKED_YUV422_UYVY,
	IMX_2D_PIXEL_FORMAT_PACKED_YUV422_YUYV,
	/*IMX_2D_PIXEL_FORMAT_PACKED_YUV422_YVYU,*/
	IMX_2D_PIXEL_FORMAT_PACKED_YUV422_VYUY,

	IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV12,
	IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV21,
	/*IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV16,*/
	IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV61,

	IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_YV12,
	IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_I420
};


/* G2D only supports RGB formats as destination. */
static Imx2dPixelFormat const supported_dest_pixel_formats[] =
{
	IMX_2D_PIXEL_FORMAT_RGBX8888,
	IMX_2D_PIXEL_FORMAT_RGBA8888,
	IMX_2D_PIXEL_FORMAT_BGRX8888,
	IMX_2D_PIXEL_FORMAT_BGRA8888,
	IMX_2D_PIXEL_FORMAT_XRGB8888,
	IMX_2D_PIXEL_FORMAT_ARGB8888,
	IMX_2D_PIXEL_FORMAT_XBGR8888,
	IMX_2D_PIXEL_FORMAT_ABGR8888,
	IMX_2D_PIXEL_FORMAT_RGB565,
	IMX_2D_PIXEL_FORMAT_BGR565,
};


static BOOL g2d_format_has_alpha(enum g2d_format format)
{
	switch (format)
	{
		case G2D_RGBA8888:
		case G2D_BGRA8888:
		case G2D_ARGB8888:
		case G2D_ABGR8888:
			return TRUE;
		default:
			return FALSE;
	}
}


static BOOL get_g2d_format(Imx2dPixelFormat imx_2d_format, enum g2d_format *fmt)
{
	BOOL ret = TRUE;

	assert(fmt != NULL);

	switch (imx_2d_format)
	{
		case IMX_2D_PIXEL_FORMAT_RGB565: *fmt = G2D_RGB565; break;
		case IMX_2D_PIXEL_FORMAT_BGR565: *fmt = G2D_BGR565; break;
		case IMX_2D_PIXEL_FORMAT_RGBX8888: *fmt = G2D_RGBX8888; break;
		case IMX_2D_PIXEL_FORMAT_RGBA8888: *fmt = G2D_RGBA8888; break;
		case IMX_2D_PIXEL_FORMAT_BGRX8888: *fmt = G2D_BGRX8888; break;
		case IMX_2D_PIXEL_FORMAT_BGRA8888: *fmt = G2D_BGRA8888; break;
		case IMX_2D_PIXEL_FORMAT_XRGB8888: *fmt = G2D_XRGB8888; break;
		case IMX_2D_PIXEL_FORMAT_ARGB8888: *fmt = G2D_ARGB8888; break;
		case IMX_2D_PIXEL_FORMAT_XBGR8888: *fmt = G2D_XBGR8888; break;
		case IMX_2D_PIXEL_FORMAT_ABGR8888: *fmt = G2D_ABGR8888; break;

		case IMX_2D_PIXEL_FORMAT_PACKED_YUV422_UYVY: *fmt = G2D_UYVY; break;
		case IMX_2D_PIXEL_FORMAT_PACKED_YUV422_YUYV: *fmt = G2D_YUYV; break;
		case IMX_2D_PIXEL_FORMAT_PACKED_YUV422_YVYU: *fmt = G2D_YVYU; break;
		case IMX_2D_PIXEL_FORMAT_PACKED_YUV422_VYUY: *fmt = G2D_VYUY; break;

		case IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV12: *fmt = G2D_NV12; break;
		case IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV21: *fmt = G2D_NV21; break;
		case IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV16: *fmt = G2D_NV16; break;
		case IMX_2D_PIXEL_FORMAT_SEMI_PLANAR_NV61: *fmt = G2D_NV61; break;

		case IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_YV12: *fmt = G2D_YV12; break;
		case IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_I420: *fmt = G2D_I420; break;

		default: ret = FALSE;
	}

	return ret;
}


static void copy_region_to_g2d_surface(struct g2d_surface *surface, Imx2dRegion const *region)
{
	if (region == NULL)
	{
		surface->left   = 0;
		surface->top    = 0;
		surface->right  = surface->width;
		surface->bottom = surface->height;
	}
	else
	{
		surface->left   = region->x1;
		surface->top    = region->y1;
		surface->right  = region->x2;
		surface->bottom = region->y2;
	}
}


static BOOL fill_g2d_surface_info(struct g2d_surface *g2d_surface, Imx2dSurface *imx_2d_surface)
{
	int i;
	imx_physical_address_t physical_address;
	Imx2dPixelFormatInfo const *fmt_info;
	Imx2dSurfaceDesc const *desc = imx_2d_surface_get_desc(imx_2d_surface);

	fmt_info = imx_2d_get_pixel_format_info(desc->format);
	if (fmt_info == NULL)
	{
		IMX_2D_LOG(ERROR, "could not get information about pixel format");
		return FALSE;
	}
	assert(fmt_info->num_planes <= 3);

	physical_address = imx_dma_buffer_get_physical_address(imx_2d_surface_get_dma_buffer(imx_2d_surface));
	if (physical_address == 0)
	{
		IMX_2D_LOG(ERROR, "could not get physical address from DMA buffer");
		return FALSE;
	}

	if (!get_g2d_format(desc->format, &(g2d_surface->format)))
	{
		IMX_2D_LOG(ERROR, "pixel format not supported by G2D");
		return FALSE;
	}

	g2d_surface->width = desc->width;
	g2d_surface->height = desc->height;
	g2d_surface->stride = desc->plane_stride[0] * 8 / fmt_info->num_first_plane_bpp;
	for (i = 0; i < fmt_info->num_planes; ++i)
		g2d_surface->planes[i] = physical_address + desc->plane_offset[i];
	for (i = fmt_info->num_planes; i < 3; ++i)
		g2d_surface->planes[i] = 0;

	/* XXX: G2D seems to use YV12 with incorrect plane order.
	 * In other words, for G2D, YV12 seems to be the same as
	 * I420. Consequently, we have to swap U/V plane addresses. */
	if (desc->format == IMX_2D_PIXEL_FORMAT_FULLY_PLANAR_YV12)
	{
		int paddr = g2d_surface->planes[1];
		g2d_surface->planes[1] = g2d_surface->planes[2];
		g2d_surface->planes[2] = paddr;
	}

	return TRUE;
}


#define DUMP_G2D_SURFACE_TO_LOG(DESC, SURFACE) \
	do { \
		IMX_2D_LOG(TRACE, \
			"%s:  planes %" IMX_PHYSICAL_ADDRESS_FORMAT " %" IMX_PHYSICAL_ADDRESS_FORMAT " %" IMX_PHYSICAL_ADDRESS_FORMAT "  left/top/right/bottom %d/%d/%d/%d  stride %d  width/height %d/%d  global_alpha %d  clrcolor %08x", \
			(DESC), \
			(SURFACE)->planes[0], (SURFACE)->planes[1], (SURFACE)->planes[2], \
			(SURFACE)->left, (SURFACE)->top, (SURFACE)->right, (SURFACE)->bottom, \
			(SURFACE)->stride, \
			(SURFACE)->width, (SURFACE)->height, \
			(SURFACE)->global_alpha, \
			(SURFACE)->clrcolor \
		); \
	} while (0)




typedef struct _Imx2dG2DBlitter Imx2dG2DBlitter;


struct _Imx2dG2DBlitter
{
	Imx2dBlitter parent;
	void *g2d_handle;
};


static void imx_2d_backend_g2d_blitter_destroy(Imx2dBlitter *blitter);

static int imx_2d_backend_g2d_blitter_start(Imx2dBlitter *blitter);
static int imx_2d_backend_g2d_blitter_finish(Imx2dBlitter *blitter);

static int imx_2d_backend_g2d_blitter_do_blit(Imx2dBlitter *blitter, Imx2dSurface *source, Imx2dSurface *dest, Imx2dBlitParams const *params);
static int imx_2d_backend_g2d_blitter_fill_region(Imx2dBlitter *blitter, Imx2dSurface *dest, Imx2dRegion const *dest_region, uint32_t fill_color);

static Imx2dHardwareCapabilities const * imx_2d_backend_g2d_blitter_get_hardware_capabilities(Imx2dBlitter *blitter);


static Imx2dBlitterClass imx_2d_backend_g2d_blitter_class =
{
	imx_2d_backend_g2d_blitter_destroy,

	imx_2d_backend_g2d_blitter_start,
	imx_2d_backend_g2d_blitter_finish,

	imx_2d_backend_g2d_blitter_do_blit,
	imx_2d_backend_g2d_blitter_fill_region,

	imx_2d_backend_g2d_blitter_get_hardware_capabilities
};




static void imx_2d_backend_g2d_blitter_destroy(Imx2dBlitter *blitter)
{
	free(blitter);
}


static int imx_2d_backend_g2d_blitter_start(Imx2dBlitter *blitter)
{
	Imx2dG2DBlitter *g2d_blitter = (Imx2dG2DBlitter *)blitter;

	if (g2d_open(&(g2d_blitter->g2d_handle)) != 0)
	{
		IMX_2D_LOG(ERROR, "opening g2d device failed");
		return FALSE;
	}

	if (g2d_make_current(g2d_blitter->g2d_handle, G2D_HARDWARE_2D) != 0)
	{
		IMX_2D_LOG(ERROR, "g2d_make_current() failed");
		if (g2d_close(g2d_blitter->g2d_handle) != 0)
			IMX_2D_LOG(ERROR, "closing g2d device failed");
		return FALSE;
	}

	return TRUE;
}


static int imx_2d_backend_g2d_blitter_finish(Imx2dBlitter *blitter)
{
	Imx2dG2DBlitter *g2d_blitter = (Imx2dG2DBlitter *)blitter;
	int ret;

	assert(blitter != NULL);

	ret = (g2d_finish(g2d_blitter->g2d_handle) == 0);

	if (g2d_close(g2d_blitter->g2d_handle) != 0)
		IMX_2D_LOG(ERROR, "closing g2d device failed");

	return ret;
}


// TODO: Source and destination regions need to be
// manually clipped. G2D does not do this automatically.
// Regions that are (partially) outside of the surface
// boundaries cause blitting to fail.


static int imx_2d_backend_g2d_blitter_do_blit(Imx2dBlitter *blitter, Imx2dSurface *source, Imx2dSurface *dest, Imx2dBlitParams const *params)
{
	BOOL do_alpha;
	int g2d_ret;
	Imx2dG2DBlitter *g2d_blitter = (Imx2dG2DBlitter *)blitter;
	struct g2d_surface g2d_source_surf, g2d_dest_surf;

	static Imx2dBlitParams const default_params =
	{
		.source_region = NULL,
		.dest_region = NULL,
		.rotation = IMX_2D_ROTATION_NONE,
		.alpha = 255
	};

	Imx2dBlitParams const *params_in_use = (params != NULL) ? params : &default_params;

	assert(blitter != NULL);
	assert(source != NULL);
	assert(dest != NULL);

	assert(g2d_blitter->g2d_handle != NULL);


	if (params_in_use->alpha > 255)
	{
		IMX_2D_LOG(ERROR, "attempting to blit with alpha value %u; maximum allowed value is 255", params_in_use->alpha);
		return FALSE;
	}


	if (!fill_g2d_surface_info(&g2d_source_surf, source) || !fill_g2d_surface_info(&g2d_dest_surf, dest))
		return FALSE;

	copy_region_to_g2d_surface(&g2d_source_surf, params_in_use->source_region);
	copy_region_to_g2d_surface(&g2d_dest_surf, params_in_use->dest_region);

	g2d_source_surf.clrcolor = g2d_dest_surf.clrcolor = 0xFF000000;

	do_alpha = (params_in_use->alpha != 255) || g2d_format_has_alpha(g2d_source_surf.format);

	// TODO: check if the g2d_enable()/g2d_disable() calls affect
	// just the immediate g2d_blit() call or _all_ g2d_blit() calls
	// until g2d_finish() is called
	if (do_alpha)
	{
		g2d_source_surf.blendfunc = G2D_SRC_ALPHA;
		g2d_dest_surf.blendfunc = G2D_ONE_MINUS_SRC_ALPHA;
		g2d_enable(g2d_blitter->g2d_handle, G2D_BLEND);

		if (params_in_use->alpha != 255)
		{
			g2d_enable(g2d_blitter->g2d_handle, G2D_GLOBAL_ALPHA);
			g2d_source_surf.global_alpha = params_in_use->alpha;
			g2d_dest_surf.global_alpha = 255 - params_in_use->alpha;
		}
		else
			g2d_disable(g2d_blitter->g2d_handle, G2D_GLOBAL_ALPHA);
	}
	else
	{
		g2d_source_surf.blendfunc = G2D_ONE;
		g2d_dest_surf.blendfunc = G2D_ZERO;
		g2d_source_surf.global_alpha = 0;
		g2d_dest_surf.global_alpha = 0;
		g2d_disable(g2d_blitter->g2d_handle, G2D_BLEND);
		g2d_disable(g2d_blitter->g2d_handle, G2D_GLOBAL_ALPHA);
	}

	g2d_source_surf.rot = g2d_dest_surf.rot = G2D_ROTATION_0;
	switch (params_in_use->rotation)
	{
		case IMX_2D_ROTATION_90:  g2d_dest_surf.rot = G2D_ROTATION_90; break;
		case IMX_2D_ROTATION_180: g2d_dest_surf.rot = G2D_ROTATION_180; break;
		case IMX_2D_ROTATION_270: g2d_dest_surf.rot = G2D_ROTATION_270; break;
		case IMX_2D_ROTATION_FLIP_HORIZONTAL: g2d_source_surf.rot = G2D_FLIP_H; break;
		case IMX_2D_ROTATION_FLIP_VERTICAL: g2d_source_surf.rot = G2D_FLIP_V; break;
		default: break;
	}

	DUMP_G2D_SURFACE_TO_LOG("blit source", &g2d_source_surf);
	DUMP_G2D_SURFACE_TO_LOG("blit dest", &g2d_dest_surf);

	g2d_ret = g2d_blit(g2d_blitter->g2d_handle, &g2d_source_surf, &g2d_dest_surf);

	if (do_alpha)
		g2d_disable(g2d_blitter->g2d_handle, G2D_BLEND);

	if (g2d_ret != 0)
	{
		IMX_2D_LOG(ERROR, "could not blit surface");
		return FALSE;
	}
	else
		return TRUE;
}


static int imx_2d_backend_g2d_blitter_fill_region(Imx2dBlitter *blitter, Imx2dSurface *dest, Imx2dRegion const *dest_region, uint32_t fill_color)
{
	Imx2dG2DBlitter *g2d_blitter = (Imx2dG2DBlitter *)blitter;
	struct g2d_surface g2d_dest_surf;
	int g2d_ret;

	assert(blitter != NULL);
	assert(dest != NULL);
	assert(dest_region != NULL);

	assert(g2d_blitter->g2d_handle != NULL);

	if (!fill_g2d_surface_info(&g2d_dest_surf, dest))
		return FALSE;

	copy_region_to_g2d_surface(&g2d_dest_surf, dest_region);
	g2d_dest_surf.clrcolor = fill_color | 0xFF000000;

	g2d_ret = g2d_clear(g2d_blitter->g2d_handle, &g2d_dest_surf);

	g2d_finish(g2d_blitter->g2d_handle);

	if (g2d_ret != 0)
	{
		IMX_2D_LOG(ERROR, "could not clear area");
		return FALSE;
	}
	else
		return TRUE;
}


static Imx2dHardwareCapabilities const * imx_2d_backend_g2d_blitter_get_hardware_capabilities(Imx2dBlitter *blitter)
{
	IMX_2D_UNUSED_PARAM(blitter);
	return imx_2d_backend_g2d_get_hardware_capabilities();
}




Imx2dBlitter* imx_2d_backend_g2d_blitter_create(void)
{
	Imx2dG2DBlitter *g2d_blitter = malloc(sizeof(Imx2dG2DBlitter));
	assert(g2d_blitter != NULL);

	memset(g2d_blitter, 0, sizeof(Imx2dG2DBlitter));

	g2d_blitter->parent.blitter_class = &imx_2d_backend_g2d_blitter_class;

	return (Imx2dBlitter *)g2d_blitter;
}


static Imx2dHardwareCapabilities const capabilities = {
	.supported_source_pixel_formats = supported_source_pixel_formats,
	.num_supported_source_pixel_formats = sizeof(supported_source_pixel_formats) / sizeof(Imx2dPixelFormat),

	.supported_dest_pixel_formats = supported_dest_pixel_formats,
	.num_supported_dest_pixel_formats = sizeof(supported_dest_pixel_formats) / sizeof(Imx2dPixelFormat),

	.min_width = 4, .max_width = INT_MAX, .width_step_size = 1,
	.min_height = 4, .max_height = INT_MAX, .height_step_size = 1
};

Imx2dHardwareCapabilities const * imx_2d_backend_g2d_get_hardware_capabilities(void)
{
	return &capabilities;
}
