#version 450 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;

out VS_OUT 
{
   vec3 worldPos;
   vec3 normal;
   vec2 texCoords;
} vs_out;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;

void main()
{
   vs_out.texCoords = aTexCoords;
   vs_out.worldPos = vec3(model * vec4(aPos, 1.0));
   vs_out.normal = mat3(model) * aNormal;

   gl_Position = projection * view * vec4(vs_out.worldPos, 1.0);
}