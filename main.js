console.log(['Homebrew', 'Computer', 'Club'].map(function(x) { return x.toUpperCase() }));

c.render_prepare();

let object_rotation = [0, 0];

while(true) {
  let current = c.packets[context];

  object_rotation[0] += 0.008; while (object_rotation[0] > 3.14) { object_rotation[0] -= 6.28 }
  object_rotation[1] += 0.012; while (object_rotation[1] > 3.14) { object_rotation[1] -= 6.28 }
  object_rotation.forEach(function(x, i) { c.set_vector(c.object_rotation, i, x) });

  /*
  c.create_local_world(local_world, object_position, object_rotation);
  c.create_world_view(world_view, camera_position, camera_rotation);
  c.create_local_screen(local_screen, local_world, world_view, view_screen);
  */

  c.render_frame();
}
