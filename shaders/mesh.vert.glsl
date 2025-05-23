#version 460
#extension GL_EXT_buffer_reference : require

struct Vertex {
    vec3 position;
    float uv_x;
    vec3 normal;
    float uv_y;
    vec4 color;
};

layout(buffer_reference, std430) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(buffer_reference, std430) readonly buffer InstanceBuffer {
    mat4 model[];
};

layout(push_constant) uniform constants {
    mat4 scale;
    VertexBuffer vertexBuffer;
    InstanceBuffer instanceBuffer;
} PushConstants;

layout(binding = 0) uniform GlobalData {
    mat4 view;
    mat4 proj;
    mat4 viewproj;
    vec4 ambientColor;
    vec4 sunlightDirection;
    vec4 sunlightColor;
} globals;

layout(location = 0) out vec2 outUv;

void main() {
    //load vertex data from device adress
    const Vertex v = PushConstants.vertexBuffer.vertices[gl_VertexIndex];
    const mat4 model = PushConstants.instanceBuffer.model[gl_InstanceIndex];

    //output data
    gl_Position = globals.viewproj * model * PushConstants.scale * vec4(v.position, 1.0f);
    outUv = vec2(v.uv_x, v.uv_y);
}
