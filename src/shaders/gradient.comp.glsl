#version 460

layout (local_size_x = 16, local_size_y = 16) in;

layout(rgba16f,set = 0, binding = 0) uniform image2D image;
layout(push_constant) uniform constants {
	float time;
}PushConstants;

void main() {
    const vec4  kRGBToYPrime = vec4 (0.299, 0.587, 0.114, 0.0);
    const vec4  kRGBToI     = vec4 (0.596, -0.275, -0.321, 0.0);
    const vec4  kRGBToQ     = vec4 (0.212, -0.523, 0.311, 0.0);

    const vec4  kYIQToR   = vec4 (1.0, 0.956, 0.621, 0.0);
    const vec4  kYIQToG   = vec4 (1.0, -0.272, -0.647, 0.0);
    const vec4  kYIQToB   = vec4 (1.0, -1.107, 1.704, 0.0);

	ivec2 texelCoord = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = imageSize(image);

    if(texelCoord.x < size.x && texelCoord.y < size.y)
    {
        vec4 color = vec4(0.0, 0.0, 0.0, 1.0);

        if(gl_LocalInvocationID.x != 0 && gl_LocalInvocationID.y != 0)
		{
			color.x = float(texelCoord.x)/(size.x);
			color.y = float(texelCoord.y)/(size.y);	

			// Convert to YIQ
			float   YPrime  = dot (color, kRGBToYPrime);
			float   I      = dot (color, kRGBToI);
			float   Q      = dot (color, kRGBToQ);

			// Calculate the hue and chroma
			float   hue     = atan (Q, I);
			float   chroma  = sqrt (I * I + Q * Q);

			// Make the user's adjustments
			hue += PushConstants.time;

			// Convert back to YIQ
			Q = chroma * sin (hue);
			I = chroma * cos (hue);

			// Convert back to RGB
			vec4    yIQ   = vec4 (YPrime, I, Q, 0.0);
			color.r = dot (yIQ, kYIQToR);
			color.g = dot (yIQ, kYIQToG);
			color.b = dot (yIQ, kYIQToB);
		}
        imageStore(image, texelCoord, color);
    }
}

