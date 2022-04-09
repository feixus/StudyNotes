#version 450 core

layout (location = 0) in vec3 position;
layout (location = 1) in vec2 texcoord;

out VS_OUT 
{
   vec3 color;
   vec2 uv;
} vs_out;

uniform mat4 vp_matrix;
uniform mat4 m_matrix;

void main()
{
   gl_Position = vp_matrix * m_matrix * vec4(position, 1.0);

   vs_out.color = position;
   vs_out.uv = texcoord;
}