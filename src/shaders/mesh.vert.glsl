#version 450
#extension GL_EXT_buffer_reference : require

layout (location = 0) out vec3 outColor;

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

layout( push_constant ) uniform constants {
	mat4 proj;
	mat4 view;
	mat4 model;
	VertexBuffer vertexBuffer;
} PushConstants;

void main() {	
	//load vertex data from device adress
	Vertex v = PushConstants.vertexBuffer.vertices[gl_VertexIndex];

	//output data
	gl_Position = PushConstants.proj * PushConstants.view  * PushConstants.model * vec4(v.position, 1.0f);
	outColor = v.color.rgb;
}

