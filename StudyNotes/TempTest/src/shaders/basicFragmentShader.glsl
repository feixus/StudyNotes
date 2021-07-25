#version 450 core

out vec4 fragColor;

// layout (binding = 0) uniform sampler2D diffuse;
uniform sampler2D diffuse;

in VS_OUT
{
   vec3 color;
   vec2 uv;
} fs_in;

void main()
{
   // fragColor = vec4(fs_in.color, 1.0);

   fragColor = texture(diffuse, fs_in.uv);
}