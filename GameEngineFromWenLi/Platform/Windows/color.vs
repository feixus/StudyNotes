#version 400

in vec3 inputPosition;
in vec3 inputColor;

out vec3 color;

uniform mat4 worldMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;

void main(void)
{
    gl_Position = worldMatrix * vec4(inputPosition, 1.0f);
    gl_Position = viewMatrix * gl_Position;
    gl_Position = projectionMatrix * gl_Position;

    color = inputColor;
}