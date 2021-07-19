#version 450 core
out vec4 fragColor;

layout (binding = 0) uniform sampler2D diffuse;

in VS_OUT
{
   vec4 color;
   vec2 uv;
} fs_in;

void main()
{
   // color = fs_in.color;
   fixed3 texColor = texture(diffuse, fs_in.uv);
   fragColor = vec4(texColor, 1.0f);
}