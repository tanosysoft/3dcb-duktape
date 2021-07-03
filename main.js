console.log(['Homebrew', 'Computer', 'Club'].map(function(x) { return x.toUpperCase() }));

var object_rotation = [0, 0];

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

  c.render_frame();
}
