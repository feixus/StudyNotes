#version 450 core

out vec4 fragColor;

in vec2 TexCoords;

uniform sampler2D baseMap;

void main()
{
   vec3 tex = texture(baseMap, TexCoords).rgb;
   
   fragColor = vec4(tex, 1.0);
}