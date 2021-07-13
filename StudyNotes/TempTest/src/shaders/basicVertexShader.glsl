#version 450 core

in vec4 position;

out VS_OUT 
{
   vec4 color;
} vs_out;

uniform mat4 vp_matrix;
uniform mat4 m_matrix;

void main()
{
   gl_Position = vp_matrix * m_matrix * position;

   vs_out.color = position;
  
}