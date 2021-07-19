#version 450 core

layout (location = 0) in vec4 position;
layout (location = 1) in vec4 texcoord;

out VS_OUT 
{
   vec4 color;
   vec2 uv;
} vs_out;

uniform mat4 vp_matrix;
uniform mat4 m_matrix;

void main()
{
   gl_Position = vp_matrix * m_matrix * position;

   vs_out.color = position;
   vs_out.uv = texcoord;
}