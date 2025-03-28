#version 460

layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D textureSampler;

void main() {
    outColor = texture(textureSampler, inUv);
}
