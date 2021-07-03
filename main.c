/*
# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# (c) 2005 Dan Peori <peori@oopo.net>
# (c) 2014 doctorxyz
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.
#
*/

#include <kernel.h>
#include <stdlib.h>
#include <tamtypes.h>
#include <math3d.h>

#include <packet.h>

#include <dma_tags.h>
#include <gif_tags.h>
#include <gs_psm.h>

#include <dma.h>

#include <graph.h>

#include <draw.h>
#include <draw3d.h>

#include "texture.c"
#include "mesh_data.c"
#include "prep/duktape.c"
#include "duk_console.c"
#include "main.js.c"

extern unsigned char texture[];

VECTOR object_position = { 0.00f, 0.00f, 0.00f, 1.00f };
VECTOR object_rotation = { 0.00f, 0.00f, 0.00f, 1.00f };

VECTOR camera_position = { 0.00f, 0.00f,  60.00f, 1.00f };
VECTOR camera_rotation = { 0.00f, 0.00f,   0.00f, 1.00f };

void init_gs(framebuffer_t *frame, zbuffer_t *z, texbuffer_t *texbuf)
{

  // Define a 32-bit 640x448 framebuffer.
  frame->width = 640;
  frame->height = 448;
  frame->mask = 0;
  frame->psm = GS_PSM_24;
  frame->address = graph_vram_allocate(frame->width,frame->height, frame->psm, GRAPH_ALIGN_PAGE);

  // Enable the zbuffer.
  z->enable = DRAW_ENABLE;
  z->mask = 0;
  z->method = ZTEST_METHOD_GREATER_EQUAL;
  z->zsm = GS_ZBUF_32;
  z->address = graph_vram_allocate(frame->width,frame->height,z->zsm, GRAPH_ALIGN_PAGE);

  // Allocate some vram for the texture buffer
  texbuf->width = 512;
  texbuf->psm = GS_PSM_24;
  texbuf->address = graph_vram_allocate(512,512,GS_PSM_24,GRAPH_ALIGN_BLOCK);

  // Initialize the screen and tie the first framebuffer to the read circuits.
  graph_initialize(frame->address,frame->width,frame->height,frame->psm,0,0);

}

void init_drawing_environment(framebuffer_t *frame, zbuffer_t *z)
{

  packet_t *packet = packet_init(20,PACKET_NORMAL);

  // This is our generic qword pointer.
  qword_t *q = packet->data;

  // This will setup a default drawing environment.
  q = draw_setup_environment(q,0,frame,z);

  // Now reset the primitive origin to 2048-width/2,2048-height/2.
  q = draw_primitive_xyoffset(q,0,(2048-(frame->width/2)),(2048-(frame->height/2)));

  // Finish setting up the environment.
  q = draw_finish(q);

  // Now send the packet, no need to wait since it's the first.
  dma_channel_send_normal(DMA_CHANNEL_GIF,packet->data,q - packet->data, 0, 0);
  dma_wait_fast();

  packet_free(packet);

}

void load_texture(texbuffer_t *texbuf)
{

  packet_t *packet = packet_init(50,PACKET_NORMAL);

  qword_t *q = packet->data;

  q = packet->data;

  q = draw_texture_transfer(q,texture,512,512,GS_PSM_24,texbuf->address,texbuf->width);
  q = draw_texture_flush(q);

  dma_channel_send_chain(DMA_CHANNEL_GIF,packet->data, q - packet->data, 0,0);
  dma_wait_fast();

  packet_free(packet);

}

void setup_texture(texbuffer_t *texbuf)
{

  packet_t *packet = packet_init(10,PACKET_NORMAL);

  qword_t *q = packet->data;

  // Using a texture involves setting up a lot of information.
  clutbuffer_t clut;

  lod_t lod;

  lod.calculation = LOD_USE_K;
  lod.max_level = 0;
  lod.mag_filter = LOD_MAG_NEAREST;
  lod.min_filter = LOD_MIN_NEAREST;
  lod.l = 0;
  lod.k = 0;

  texbuf->info.width = draw_log2(512);
  texbuf->info.height = draw_log2(512);
  texbuf->info.components = TEXTURE_COMPONENTS_RGB;
  texbuf->info.function = TEXTURE_FUNCTION_DECAL;

  clut.storage_mode = CLUT_STORAGE_MODE1;
  clut.start = 0;
  clut.psm = 0;
  clut.load_method = CLUT_NO_LOAD;
  clut.address = 0;

  q = draw_texture_sampling(q,0,&lod);
  q = draw_texturebuffer(q,0,texbuf,&clut);

  // Now send the packet, no need to wait since it's the first.
  dma_channel_send_normal(DMA_CHANNEL_GIF,packet->data,q - packet->data, 0, 0);
  dma_wait_fast();

  packet_free(packet);

}

int i;
int context = 0;

packet_t *packets[2];
packet_t *current;

qword_t *q;
u64 *dw;

MATRIX local_world;
MATRIX world_view;
MATRIX view_screen;
MATRIX local_screen;

prim_t prim;
color_t color;

VECTOR *temp_vertices;

xyz_t *xyz;
color_t *rgbaq;
texel_t *st;

framebuffer_t frame;
zbuffer_t z;

duk_ret_t render_prepare() {
  packets[0] = packet_init(100,PACKET_NORMAL);
  packets[1] = packet_init(100,PACKET_NORMAL);

  // Define the triangle primitive we want to use.
  prim.type = PRIM_TRIANGLE;
  prim.shading = PRIM_SHADE_GOURAUD;
  prim.mapping = DRAW_ENABLE;
  prim.fogging = DRAW_DISABLE;
  prim.blending = DRAW_ENABLE;
  prim.antialiasing = DRAW_DISABLE;
  prim.mapping_type = PRIM_MAP_ST;
  prim.colorfix = PRIM_UNFIXED;

  color.r = 0x80;
  color.g = 0x80;
  color.b = 0x80;
  color.a = 0x40;
  color.q = 1.0f;

  // Allocate calculation space.
  temp_vertices = memalign(128, sizeof(VECTOR) * vertex_count);

  // Allocate register space.
  xyz   = memalign(128, sizeof(u64) * vertex_count);
  rgbaq = memalign(128, sizeof(u64) * vertex_count);
  st    = memalign(128, sizeof(u64) * vertex_count);

  // Create the view_screen matrix.
  create_view_screen(view_screen, graph_aspect_ratio(), -3.00f, 3.00f, -3.00f, 3.00f, 1.00f, 2000.00f);

  return 0;
}

duk_ret_t render_frame() {
  current = packets[context];

  q = current->data;

  // Clear framebuffer but don't update zbuffer.
  q = draw_disable_tests(q,0,&z);
  q = draw_clear(q,0,2048.0f-320.0f,2048.0f-256.0f,frame.width,frame.height,0x40,0x40,0x40);
  q = draw_enable_tests(q,0,&z);

  // Draw the triangles using triangle primitive type.
  // Use a 64-bit pointer to simplify adding data to the packet.
  dw = (u64*)draw_prim_start(q,0,&prim, &color);

  for(i = 0; i < points_count; i++)
  {
    *dw++ = rgbaq[points[i]].rgbaq;
    *dw++ = st[points[i]].uv;
    *dw++ = xyz[points[i]].xyz;
  }

  // Check if we're in middle of a qword or not.
  if ((u32)dw % 16)
  {

    *dw++ = 0;

  }

  // Only 3 registers rgbaq/st/xyz were used (standard STQ reglist)
  q = draw_prim_end((qword_t*)dw,3,DRAW_STQ_REGLIST);

  // Setup a finish event.
  q = draw_finish(q);

  // Now send our current dma chain.
  dma_wait_fast();
  dma_channel_send_normal(DMA_CHANNEL_GIF,current->data, q - current->data, 0, 0);

  // Now switch our packets so we can process data while the DMAC is working.
  context ^= 1;

  // Wait for scene to finish drawing
  draw_wait_finish();

  graph_wait_vsync();

  return 0;
}

duk_ret_t set_vector(duk_context *ctx) {
  VECTOR *v = duk_get_pointer(ctx, 0);
  int i = (int) duk_get_number(ctx, 1);
  float x = (float) duk_get_number(ctx, 2);
  (*v)[i] = x;
  return 0;
}

duk_ret_t create_local_world_thunk(duk_context * ctx) {
  void * _var0 = (void * ) duk_get_pointer(ctx, 0);
  void * _var1 = (void * ) duk_get_pointer(ctx, 1);
  void * _var2 = (void * ) duk_get_pointer(ctx, 2);
  create_local_world(_var0, _var1, _var2);
  return 0;
}
duk_ret_t create_world_view_thunk(duk_context * ctx) {
  void * _var0 = (void * ) duk_get_pointer(ctx, 0);
  void * _var1 = (void * ) duk_get_pointer(ctx, 1);
  void * _var2 = (void * ) duk_get_pointer(ctx, 2);
  create_world_view(_var0, _var1, _var2);
  return 0;
}
duk_ret_t create_local_screen_thunk(duk_context * ctx) {
  void * _var0 = (void * ) duk_get_pointer(ctx, 0);
  void * _var1 = (void * ) duk_get_pointer(ctx, 1);
  void * _var2 = (void * ) duk_get_pointer(ctx, 2);
  void * _var3 = (void * ) duk_get_pointer(ctx, 3);
  create_local_screen(_var0, _var1, _var2, _var3);
  return 0;
}
duk_ret_t calculate_vertices_thunk(duk_context * ctx) {
  void * _var0 = (void * ) duk_get_pointer(ctx, 0);
  unsigned int _var1 = (unsigned int) duk_get_uint(ctx, 1);
  void * _var2 = (void * ) duk_get_pointer(ctx, 2);
  void * _var3 = (void * ) duk_get_pointer(ctx, 3);
  calculate_vertices(_var0, _var1, _var2, _var3);
  return 0;
}
duk_ret_t draw_convert_xyz_thunk(duk_context * ctx) {
  void * _var0 = (void * ) duk_get_pointer(ctx, 0);
  int _var1 = (int) duk_get_int(ctx, 1);
  int _var2 = (int) duk_get_int(ctx, 2);
  int _var3 = (int) duk_get_int(ctx, 3);
  unsigned int _var4 = (unsigned int) duk_get_uint(ctx, 4);
  void * _var5 = (void * ) duk_get_pointer(ctx, 5);
  draw_convert_xyz(_var0, _var1, _var2, _var3, _var4, _var5);
  return 0;
}
duk_ret_t draw_convert_rgbq_thunk(duk_context * ctx) {
  void * _var0 = (void * ) duk_get_pointer(ctx, 0);
  unsigned int _var1 = (unsigned int) duk_get_uint(ctx, 1);
  void * _var2 = (void * ) duk_get_pointer(ctx, 2);
  void * _var3 = (void * ) duk_get_pointer(ctx, 3);
  int _var4 = (int) duk_get_int(ctx, 4);
  draw_convert_rgbq(_var0, _var1, _var2, _var3, _var4);
  return 0;
}
duk_ret_t draw_convert_st_thunk(duk_context * ctx) {
  void * _var0 = (void * ) duk_get_pointer(ctx, 0);
  unsigned int _var1 = (unsigned int) duk_get_uint(ctx, 1);
  void * _var2 = (void * ) duk_get_pointer(ctx, 2);
  void * _var3 = (void * ) duk_get_pointer(ctx, 3);
  draw_convert_st(_var0, _var1, _var2, _var3);
  return 0;
}
int main(int argc, char **argv) {
  duk_context *ctx;

  ctx = duk_create_heap_default();
  if (!ctx) { return 1; }

  duk_console_init(ctx, DUK_CONSOLE_PROXY_WRAPPER /*flags*/);
  printf("top after init: %ld\n", (long) duk_get_top(ctx));

  // The buffers to be used.
  //framebuffer_t frame;
  //zbuffer_t z;
  texbuffer_t texbuf;

  // Init GIF dma channel.
  dma_channel_initialize(DMA_CHANNEL_GIF,NULL,0);
  dma_channel_fast_waits(DMA_CHANNEL_GIF);

  // Init the GS, framebuffer, zbuffer, and texture buffer.
  init_gs(&frame, &z, &texbuf);

  // Init the drawing environment and framebuffer.
  init_drawing_environment(&frame,&z);

  // Load the texture into vram.
  load_texture(&texbuf);

  // Setup texture buffer
  setup_texture(&texbuf);

  render_prepare();

  duk_push_object(ctx);

  duk_push_c_function(ctx, render_frame, 0); duk_put_prop_string(ctx, -2, "render_frame");
  duk_push_c_function(ctx, set_vector, 3); duk_put_prop_string(ctx, -2, "set_vector");
  duk_push_c_function(ctx, create_local_world_thunk, 3); duk_put_prop_string(ctx, -2, "create_local_world");
  duk_push_c_function(ctx, create_world_view_thunk, 3); duk_put_prop_string(ctx, -2, "create_world_view");
  duk_push_c_function(ctx, create_local_screen_thunk, 4); duk_put_prop_string(ctx, -2, "create_local_screen");
  duk_push_c_function(ctx, calculate_vertices_thunk, 3); duk_put_prop_string(ctx, -2, "calculate_vertices");
  duk_push_c_function(ctx, draw_convert_xyz_thunk, 6); duk_put_prop_string(ctx, -2, "draw_convert_xyz");
  duk_push_c_function(ctx, draw_convert_rgbq_thunk, 5); duk_put_prop_string(ctx, -2, "draw_convert_rgbq");
  duk_push_c_function(ctx, draw_convert_st_thunk, 4); duk_put_prop_string(ctx, -2, "draw_convert_st");

  duk_push_pointer(ctx, object_position); duk_put_prop_string(ctx, -2, "object_position");
  duk_push_pointer(ctx, object_rotation); duk_put_prop_string(ctx, -2, "object_rotation");
  duk_push_pointer(ctx, camera_position); duk_put_prop_string(ctx, -2, "camera_position");
  duk_push_pointer(ctx, camera_rotation); duk_put_prop_string(ctx, -2, "camera_rotation");
  duk_push_pointer(ctx, local_world); duk_put_prop_string(ctx, -2, "local_world");
  duk_push_pointer(ctx, world_view); duk_put_prop_string(ctx, -2, "world_view");
  duk_push_pointer(ctx, local_screen); duk_put_prop_string(ctx, -2, "local_screen");
  duk_push_pointer(ctx, view_screen); duk_put_prop_string(ctx, -2, "view_screen");
  duk_push_pointer(ctx, temp_vertices); duk_put_prop_string(ctx, -2, "temp_vertices");
  duk_push_pointer(ctx, vertices); duk_put_prop_string(ctx, -2, "vertices");
  duk_push_uint(ctx, vertex_count); duk_put_prop_string(ctx, -2, "vertex_count");
  duk_push_pointer(ctx, xyz); duk_put_prop_string(ctx, -2, "xyz");
  duk_push_pointer(ctx, rgbaq); duk_put_prop_string(ctx, -2, "rgbaq");
  duk_push_pointer(ctx, colours); duk_put_prop_string(ctx, -2, "colours");
  duk_push_pointer(ctx, st); duk_put_prop_string(ctx, -2, "st");
  duk_push_pointer(ctx, coordinates); duk_put_prop_string(ctx, -2, "coordinates");

  duk_push_array(ctx);
  duk_push_pointer(ctx, packets[0]);
  duk_put_prop_index(ctx, -2, 0);
  duk_push_pointer(ctx, packets[1]);
  duk_put_prop_index(ctx, -2, 0);
  duk_put_prop_string(ctx, -2, "packets");

  duk_put_global_string(ctx, "c");

  // Run JavaScript
  if (duk_peval_lstring(ctx, (const char *)javascript, size_javascript) != 0) {
    printf("DUK ERROR!\n");
    printf("--> %s\n", duk_safe_to_string(ctx, -1));
    return 1;
  }

  duk_destroy_heap(ctx);
  free(packets[0]);
  free(packets[1]);

  // Sleep
  SleepThread();

  // End program.
  return 0;

}
