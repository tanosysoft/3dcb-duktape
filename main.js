console.log(['Homebrew', 'Computer', 'Club'].map(function(x) { return x.toUpperCase() }));

var object_rotation = [0, 0];
var context = 0;
var current, q_start, q, dw, i;

while(true) {
  object_rotation[0] += 0.008; while (object_rotation[0] > 3.14) { object_rotation[0] -= 6.28 }
  object_rotation[1] += 0.012; while (object_rotation[1] > 3.14) { object_rotation[1] -= 6.28 }
  object_rotation.forEach(function(x, i) { c.set_vector(c.object_rotation, i, x) });

  c.create_local_world(c.local_world, c.object_position, c.object_rotation);
  c.create_world_view(c.world_view, c.camera_position, c.camera_rotation);
  c.create_local_screen(c.local_screen, c.local_world, c.world_view, c.view_screen);
  c.calculate_vertices(c.temp_vertices, c.vertex_count, c.vertices, c.local_screen);
  c.draw_convert_xyz(c.xyz, 2048, 2048, 32, c.vertex_count, c.temp_vertices);
  c.draw_convert_rgbq(c.rgbaq, c.vertex_count, c.temp_vertices, c.colours, 0x40);
  c.draw_convert_st(c.st, c.vertex_count, c.temp_vertices, c.coordinates);

  current = c.packets[context];
  context ^= 1;
  q_start = q = c.get_packet_data(current);

  // Clear framebuffer but don't update zbuffer.
  q = c.draw_disable_tests(q, 0, c.z);
  q = c.draw_clear(q, 0, 2048 - 320, 2048 - 256, 640, 448, 0x40, 0x40, 0x40);
  q = c.draw_enable_tests(q, 0, c.z);

  // Draw the triangles using triangle primitive type.
  // Use a 64-bit pointer to simplify adding data to the packet.
  dw = c.draw_prim_start(q, 0, c.prim, c.color);

  //for (i = 0; i < c.points_count; i++) {
    //c.memwrite_u64(dw, c.ptradd(c.rgbaq, 8 * c.memread_int(c.points, i))); dw += 8;
    //c.memwrite_u64(dw, c.ptradd(c.st, 8 * c.memread_int(c.points, i))); dw += 8;
    //c.memwrite_u64(dw, c.ptradd(c.xyz, 8 * c.memread_int(c.points, i))); dw += 8;
  //}

  // Check if we're in middle of a qword or not.
  //if (dw % 16) { memwrite_u64(dw, 0); dw += 8 }

  // Only 3 registers rgbaq/st/xyz were used (standard STQ reglist)
  q = c.draw_prim_end(dw, 3, c.DRAW_STQ_REGLIST);

  // Setup a finish event.
  q = c.draw_finish(q);

  // Now send our current dma chain.
  c.dma_wait_fast();
  c.dma_channel_send_normal(c.DMA_CHANNEL_GIF, q_start, c.ptrsub(q, q_start), 0, 0);

  // Now switch our packets so we can process data while the DMAC is working.
  context ^= 1;

  // Wait for scene to finish drawing
  c.draw_wait_finish();

  c.graph_wait_vsync();
}
