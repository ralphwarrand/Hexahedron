#version 420 core
layout(location = 0) in vec3 position;

uniform mat4 light_view;
uniform mat4 light_projection;

void main()
{
    gl_Position = light_projection * light_view * vec4(position, 1.0);
}