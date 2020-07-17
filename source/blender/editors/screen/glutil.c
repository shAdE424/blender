/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 */

/** \file
 * \ingroup edscr
 */

#include <stdio.h>
#include <string.h>

#include "DNA_userdef_types.h"
#include "DNA_vec_types.h"

#include "BLI_math.h"
#include "BLI_utildefines.h"

#include "BKE_context.h"

#include "BIF_glutil.h"

#include "IMB_colormanagement.h"
#include "IMB_imbuf_types.h"

#include "GPU_immediate.h"
#include "GPU_matrix.h"
#include "GPU_texture.h"

#ifdef __APPLE__
#  include "GPU_state.h"
#endif

#include "UI_interface.h"

/* ******************************************** */

static void immDrawPixelsTexSetupAttributes(IMMDrawPixelsTexState *state)
{
  GPUVertFormat *vert_format = immVertexFormat();
  state->pos = GPU_vertformat_attr_add(vert_format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
  state->texco = GPU_vertformat_attr_add(
      vert_format, "texCoord", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
}

/* To be used before calling immDrawPixelsTex
 * Default shader is GPU_SHADER_2D_IMAGE_COLOR
 * You can still set uniforms with :
 * GPU_shader_uniform_int(shader, GPU_shader_get_uniform(shader, "name"), 0);
 * */
IMMDrawPixelsTexState immDrawPixelsTexSetup(int builtin)
{
  IMMDrawPixelsTexState state;
  immDrawPixelsTexSetupAttributes(&state);

  state.shader = GPU_shader_get_builtin_shader(builtin);

  /* Shader will be unbind by immUnbindProgram in immDrawPixelsTexScaled_clipping */
  immBindBuiltinProgram(builtin);
  immUniform1i("image", 0);
  state.do_shader_unbind = true;

  return state;
}

/* Use the currently bound shader.
 *
 * Use immDrawPixelsTexSetup to bind the shader you
 * want before calling immDrawPixelsTex.
 *
 * If using a special shader double check it uses the same
 * attributes "pos" "texCoord" and uniform "image".
 *
 * If color is NULL then use white by default
 *
 * Be also aware that this function unbinds the shader when
 * it's finished.
 * */
void immDrawPixelsTexScaled_clipping(IMMDrawPixelsTexState *state,
                                     float x,
                                     float y,
                                     int img_w,
                                     int img_h,
                                     int format,
                                     int type,
                                     int zoomfilter,
                                     void *rect,
                                     float scaleX,
                                     float scaleY,
                                     float clip_min_x,
                                     float clip_min_y,
                                     float clip_max_x,
                                     float clip_max_y,
                                     float xzoom,
                                     float yzoom,
                                     float color[4])
{
  int subpart_x, subpart_y, tex_w = 256, tex_h = 256;
  int seamless, offset_x, offset_y, nsubparts_x, nsubparts_y;
  int components;
  const bool use_clipping = ((clip_min_x < clip_max_x) && (clip_min_y < clip_max_y));
  float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};

  if (type != GL_FLOAT) {
    BLI_assert(type == GL_UNSIGNED_BYTE);
    type = GL_UNSIGNED_BYTE;
  }

  GLint unpack_row_length;
  glGetIntegerv(GL_UNPACK_ROW_LENGTH, &unpack_row_length);

  eGPUTextureFormat gpu_format = (type == GL_FLOAT) ? GPU_RGBA16F : GPU_RGBA8;
  eGPUDataFormat gpu_data = (type == GL_FLOAT) ? GPU_DATA_FLOAT : GPU_DATA_UNSIGNED_BYTE;
  GPUTexture *texture = GPU_texture_create_nD(
      tex_w, tex_h, 0, 2, NULL, gpu_format, gpu_data, 0, false, NULL);

  /* TODO replace GL_NEAREST/LINEAR in callers. */
  GPU_texture_filter_mode(texture, (zoomfilter == GL_LINEAR));
  GPU_texture_wrap_mode(texture, false, true);

  GPU_texture_bind(texture, 0);

  /* setup seamless 2=on, 0=off */
  seamless = ((tex_w < img_w || tex_h < img_h) && tex_w > 2 && tex_h > 2) ? 2 : 0;

  offset_x = tex_w - seamless;
  offset_y = tex_h - seamless;

  nsubparts_x = (img_w + (offset_x - 1)) / (offset_x);
  nsubparts_y = (img_h + (offset_y - 1)) / (offset_y);

  if (format == GL_RGBA) {
    components = 4;
  }
  else if (format == GL_RGB) {
    components = 3;
  }
  else if (format == GL_RED) {
    components = 1;
  }
  else {
    BLI_assert(!"Incompatible format passed to glaDrawPixelsTexScaled");
    return;
  }

  /* optional */
  /* NOTE: Shader could be null for GLSL OCIO drawing, it is fine, since
   * it does not need color.
   */
  if (state->shader != NULL && GPU_shader_get_uniform(state->shader, "color") != -1) {
    immUniformColor4fv((color) ? color : white);
  }

  glPixelStorei(GL_UNPACK_ROW_LENGTH, img_w);

  for (subpart_y = 0; subpart_y < nsubparts_y; subpart_y++) {
    for (subpart_x = 0; subpart_x < nsubparts_x; subpart_x++) {
      int remainder_x = img_w - subpart_x * offset_x;
      int remainder_y = img_h - subpart_y * offset_y;
      int subpart_w = (remainder_x < tex_w) ? remainder_x : tex_w;
      int subpart_h = (remainder_y < tex_h) ? remainder_y : tex_h;
      int offset_left = (seamless && subpart_x != 0) ? 1 : 0;
      int offset_bot = (seamless && subpart_y != 0) ? 1 : 0;
      int offset_right = (seamless && remainder_x > tex_w) ? 1 : 0;
      int offset_top = (seamless && remainder_y > tex_h) ? 1 : 0;
      float rast_x = x + subpart_x * offset_x * xzoom;
      float rast_y = y + subpart_y * offset_y * yzoom;
      /* check if we already got these because we always get 2 more when doing seamless */
      if (subpart_w <= seamless || subpart_h <= seamless) {
        continue;
      }

      int right = subpart_w - offset_right;
      int top = subpart_h - offset_top;
      int bottom = 0 + offset_bot;
      int left = 0 + offset_left;

      if (use_clipping) {
        if (rast_x + right * xzoom * scaleX < clip_min_x ||
            rast_y + top * yzoom * scaleY < clip_min_y) {
          continue;
        }
        if (rast_x + left * xzoom > clip_max_x || rast_y + bottom * yzoom > clip_max_y) {
          continue;
        }
      }

      {
        int src_y = subpart_y * offset_y;
        int src_x = subpart_x * offset_x;
        size_t stride = components * ((type == GL_FLOAT) ? sizeof(float) : sizeof(uchar));

#define DATA(_y, _x) ((char *)rect + stride * ((size_t)(_y)*img_w + (_x)))
        {
          void *data = DATA(src_y, src_x);
          glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, subpart_w, subpart_h, format, type, data);
        }
        /* Add an extra border of pixels so linear interpolation looks ok
         * at edges of full image. */
        if (subpart_w < tex_w) {
          void *data = DATA(src_y, src_x + subpart_w - 1);
          glTexSubImage2D(GL_TEXTURE_2D, 0, subpart_w, 0, 1, subpart_h, format, type, data);
        }
        if (subpart_h < tex_h) {
          void *data = DATA(src_y + subpart_h - 1, src_x);
          glTexSubImage2D(GL_TEXTURE_2D, 0, 0, subpart_h, subpart_w, 1, format, type, data);
        }
        if (subpart_w < tex_w && subpart_h < tex_h) {
          void *data = DATA(src_y + subpart_h - 1, src_x + subpart_w - 1);
          glTexSubImage2D(GL_TEXTURE_2D, 0, subpart_w, subpart_h, 1, 1, format, type, data);
        }
#undef DATA
      }

      uint pos = state->pos, texco = state->texco;

      immBegin(GPU_PRIM_TRI_FAN, 4);
      immAttr2f(texco, left / (float)tex_w, bottom / (float)tex_h);
      immVertex2f(pos, rast_x + offset_left * xzoom, rast_y + offset_bot * yzoom);

      immAttr2f(texco, right / (float)tex_w, bottom / (float)tex_h);
      immVertex2f(pos, rast_x + right * xzoom * scaleX, rast_y + offset_bot * yzoom);

      immAttr2f(texco, right / (float)tex_w, top / (float)tex_h);
      immVertex2f(pos, rast_x + right * xzoom * scaleX, rast_y + top * yzoom * scaleY);

      immAttr2f(texco, left / (float)tex_w, top / (float)tex_h);
      immVertex2f(pos, rast_x + offset_left * xzoom, rast_y + top * yzoom * scaleY);
      immEnd();

      /* NOTE: Weirdly enough this is only required on macOS. Without this there is some sort of
       * bleeding of data is happening from tiles which are drawn later on.
       * This doesn't seem to be too slow,
       * but still would be nice to have fast and nice solution. */
#ifdef __APPLE__
      GPU_flush();
#endif
    }
  }

  if (state->do_shader_unbind) {
    immUnbindProgram();
  }

  GPU_texture_unbind(texture);
  GPU_texture_free(texture);

  glPixelStorei(GL_UNPACK_ROW_LENGTH, unpack_row_length);
}

void immDrawPixelsTexScaled(IMMDrawPixelsTexState *state,
                            float x,
                            float y,
                            int img_w,
                            int img_h,
                            int format,
                            int type,
                            int zoomfilter,
                            void *rect,
                            float scaleX,
                            float scaleY,
                            float xzoom,
                            float yzoom,
                            float color[4])
{
  immDrawPixelsTexScaled_clipping(state,
                                  x,
                                  y,
                                  img_w,
                                  img_h,
                                  format,
                                  type,
                                  zoomfilter,
                                  rect,
                                  scaleX,
                                  scaleY,
                                  0.0f,
                                  0.0f,
                                  0.0f,
                                  0.0f,
                                  xzoom,
                                  yzoom,
                                  color);
}

void immDrawPixelsTex(IMMDrawPixelsTexState *state,
                      float x,
                      float y,
                      int img_w,
                      int img_h,
                      int format,
                      int type,
                      int zoomfilter,
                      void *rect,
                      float xzoom,
                      float yzoom,
                      float color[4])
{
  immDrawPixelsTexScaled_clipping(state,
                                  x,
                                  y,
                                  img_w,
                                  img_h,
                                  format,
                                  type,
                                  zoomfilter,
                                  rect,
                                  1.0f,
                                  1.0f,
                                  0.0f,
                                  0.0f,
                                  0.0f,
                                  0.0f,
                                  xzoom,
                                  yzoom,
                                  color);
}

void immDrawPixelsTex_clipping(IMMDrawPixelsTexState *state,
                               float x,
                               float y,
                               int img_w,
                               int img_h,
                               int format,
                               int type,
                               int zoomfilter,
                               void *rect,
                               float clip_min_x,
                               float clip_min_y,
                               float clip_max_x,
                               float clip_max_y,
                               float xzoom,
                               float yzoom,
                               float color[4])
{
  immDrawPixelsTexScaled_clipping(state,
                                  x,
                                  y,
                                  img_w,
                                  img_h,
                                  format,
                                  type,
                                  zoomfilter,
                                  rect,
                                  1.0f,
                                  1.0f,
                                  clip_min_x,
                                  clip_min_y,
                                  clip_max_x,
                                  clip_max_y,
                                  xzoom,
                                  yzoom,
                                  color);
}

/* **** Color management helper functions for GLSL display/transform ***** */

/* Draw given image buffer on a screen using GLSL for display transform */
void ED_draw_imbuf_clipping(ImBuf *ibuf,
                            float x,
                            float y,
                            int zoomfilter,
                            ColorManagedViewSettings *view_settings,
                            ColorManagedDisplaySettings *display_settings,
                            float clip_min_x,
                            float clip_min_y,
                            float clip_max_x,
                            float clip_max_y,
                            float zoom_x,
                            float zoom_y)
{
  bool force_fallback = false;
  bool need_fallback = true;

  /* Early out */
  if (ibuf->rect == NULL && ibuf->rect_float == NULL) {
    return;
  }

  /* Single channel images could not be transformed using GLSL yet */
  force_fallback |= ibuf->channels == 1;

  /* If user decided not to use GLSL, fallback to glaDrawPixelsAuto */
  force_fallback |= (ED_draw_imbuf_method(ibuf) != IMAGE_DRAW_METHOD_GLSL);

  /* Try to draw buffer using GLSL display transform */
  if (force_fallback == false) {
    int ok;

    IMMDrawPixelsTexState state = {0};
    /* We want GLSL state to be fully handled by OCIO. */
    state.do_shader_unbind = false;
    immDrawPixelsTexSetupAttributes(&state);

    if (ibuf->rect_float) {
      if (ibuf->float_colorspace) {
        ok = IMB_colormanagement_setup_glsl_draw_from_space(
            view_settings, display_settings, ibuf->float_colorspace, ibuf->dither, true, false);
      }
      else {
        ok = IMB_colormanagement_setup_glsl_draw(
            view_settings, display_settings, ibuf->dither, true);
      }
    }
    else {
      ok = IMB_colormanagement_setup_glsl_draw_from_space(
          view_settings, display_settings, ibuf->rect_colorspace, ibuf->dither, false, false);
    }

    if (ok) {
      if (ibuf->rect_float) {
        int format = 0;

        if (ibuf->channels == 3) {
          format = GL_RGB;
        }
        else if (ibuf->channels == 4) {
          format = GL_RGBA;
        }
        else {
          BLI_assert(!"Incompatible number of channels for GLSL display");
        }

        if (format != 0) {
          immDrawPixelsTex_clipping(&state,
                                    x,
                                    y,
                                    ibuf->x,
                                    ibuf->y,
                                    format,
                                    GL_FLOAT,
                                    zoomfilter,
                                    ibuf->rect_float,
                                    clip_min_x,
                                    clip_min_y,
                                    clip_max_x,
                                    clip_max_y,
                                    zoom_x,
                                    zoom_y,
                                    NULL);
        }
      }
      else if (ibuf->rect) {
        /* ibuf->rect is always RGBA */
        immDrawPixelsTex_clipping(&state,
                                  x,
                                  y,
                                  ibuf->x,
                                  ibuf->y,
                                  GL_RGBA,
                                  GL_UNSIGNED_BYTE,
                                  zoomfilter,
                                  ibuf->rect,
                                  clip_min_x,
                                  clip_min_y,
                                  clip_max_x,
                                  clip_max_y,
                                  zoom_x,
                                  zoom_y,
                                  NULL);
      }

      IMB_colormanagement_finish_glsl_draw();

      need_fallback = false;
    }
  }

  /* In case GLSL failed or not usable, fallback to glaDrawPixelsAuto */
  if (need_fallback) {
    uchar *display_buffer;
    void *cache_handle;

    display_buffer = IMB_display_buffer_acquire(
        ibuf, view_settings, display_settings, &cache_handle);

    if (display_buffer) {
      IMMDrawPixelsTexState state = immDrawPixelsTexSetup(GPU_SHADER_2D_IMAGE_COLOR);
      immDrawPixelsTex_clipping(&state,
                                x,
                                y,
                                ibuf->x,
                                ibuf->y,
                                GL_RGBA,
                                GL_UNSIGNED_BYTE,
                                zoomfilter,
                                display_buffer,
                                clip_min_x,
                                clip_min_y,
                                clip_max_x,
                                clip_max_y,
                                zoom_x,
                                zoom_y,
                                NULL);
    }

    IMB_display_buffer_release(cache_handle);
  }
}

void ED_draw_imbuf(ImBuf *ibuf,
                   float x,
                   float y,
                   int zoomfilter,
                   ColorManagedViewSettings *view_settings,
                   ColorManagedDisplaySettings *display_settings,
                   float zoom_x,
                   float zoom_y)
{
  ED_draw_imbuf_clipping(ibuf,
                         x,
                         y,
                         zoomfilter,
                         view_settings,
                         display_settings,
                         0.0f,
                         0.0f,
                         0.0f,
                         0.0f,
                         zoom_x,
                         zoom_y);
}

void ED_draw_imbuf_ctx_clipping(const bContext *C,
                                ImBuf *ibuf,
                                float x,
                                float y,
                                int zoomfilter,
                                float clip_min_x,
                                float clip_min_y,
                                float clip_max_x,
                                float clip_max_y,
                                float zoom_x,
                                float zoom_y)
{
  ColorManagedViewSettings *view_settings;
  ColorManagedDisplaySettings *display_settings;

  IMB_colormanagement_display_settings_from_ctx(C, &view_settings, &display_settings);

  ED_draw_imbuf_clipping(ibuf,
                         x,
                         y,
                         zoomfilter,
                         view_settings,
                         display_settings,
                         clip_min_x,
                         clip_min_y,
                         clip_max_x,
                         clip_max_y,
                         zoom_x,
                         zoom_y);
}

void ED_draw_imbuf_ctx(
    const bContext *C, ImBuf *ibuf, float x, float y, int zoomfilter, float zoom_x, float zoom_y)
{
  ED_draw_imbuf_ctx_clipping(C, ibuf, x, y, zoomfilter, 0.0f, 0.0f, 0.0f, 0.0f, zoom_x, zoom_y);
}

int ED_draw_imbuf_method(ImBuf *ibuf)
{
  if (U.image_draw_method == IMAGE_DRAW_METHOD_AUTO) {
    /* Use faster GLSL when CPU to GPU transfer is unlikely to be a bottleneck,
     * otherwise do color management on CPU side. */
    const size_t threshold = 2048 * 2048 * 4 * sizeof(float);
    const size_t data_size = (ibuf->rect_float) ? sizeof(float) : sizeof(uchar);
    const size_t size = ibuf->x * ibuf->y * ibuf->channels * data_size;

    return (size > threshold) ? IMAGE_DRAW_METHOD_2DTEXTURE : IMAGE_DRAW_METHOD_GLSL;
  }
  return U.image_draw_method;
}

/* don't move to GPU_immediate_util.h because this uses user-prefs
 * and isn't very low level */
void immDrawBorderCorners(uint pos, const rcti *border, float zoomx, float zoomy)
{
  float delta_x = 4.0f * UI_DPI_FAC / zoomx;
  float delta_y = 4.0f * UI_DPI_FAC / zoomy;

  delta_x = min_ff(delta_x, border->xmax - border->xmin);
  delta_y = min_ff(delta_y, border->ymax - border->ymin);

  /* left bottom corner */
  immBegin(GPU_PRIM_LINE_STRIP, 3);
  immVertex2f(pos, border->xmin, border->ymin + delta_y);
  immVertex2f(pos, border->xmin, border->ymin);
  immVertex2f(pos, border->xmin + delta_x, border->ymin);
  immEnd();

  /* left top corner */
  immBegin(GPU_PRIM_LINE_STRIP, 3);
  immVertex2f(pos, border->xmin, border->ymax - delta_y);
  immVertex2f(pos, border->xmin, border->ymax);
  immVertex2f(pos, border->xmin + delta_x, border->ymax);
  immEnd();

  /* right bottom corner */
  immBegin(GPU_PRIM_LINE_STRIP, 3);
  immVertex2f(pos, border->xmax - delta_x, border->ymin);
  immVertex2f(pos, border->xmax, border->ymin);
  immVertex2f(pos, border->xmax, border->ymin + delta_y);
  immEnd();

  /* right top corner */
  immBegin(GPU_PRIM_LINE_STRIP, 3);
  immVertex2f(pos, border->xmax - delta_x, border->ymax);
  immVertex2f(pos, border->xmax, border->ymax);
  immVertex2f(pos, border->xmax, border->ymax - delta_y);
  immEnd();
}
