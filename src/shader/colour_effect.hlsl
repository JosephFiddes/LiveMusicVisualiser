// OBS-specific syntax adaptation to HLSL standard to avoid errors reported by the code editor
#define SamplerState sampler_state
#define Texture2D texture2d

// Constants
#define PI 3.141592653589793238

// Uniform variables set by OBS (required)
uniform float4x4 ViewProj; // View-projection matrix used in the vertex shader
uniform Texture2D image; // Texture containing the source picture

//// Pattern texture
//uniform Texture2D pattern_texture;
//uniform float2 pattern_size = {-1.0, -1.0};
//uniform float pattern_gamma = 1.0;

//// Palette texture
//uniform Texture2D palette_texture;
//uniform float2 palette_size = {-1.0, -1.0};
//uniform float palette_gamma = 1.0;

//// General properties
//uniform float gamma = 1.2;
//uniform float gamma_shift = 0.6;
//uniform float amplitude = 0.2;
//uniform float offset = 0.0;
//uniform float scale = 1.0;
//uniform int number_of_color_levels = 4.0;

uniform float movement_strength = 0.5;
uniform float max_sample_max = 0.5;

// Size of the source picture
uniform int width;
uniform int height;

// Interpolation method and wrap mode for sampling a texture
SamplerState linear_clamp
{
    Filter = Linear; // Anisotropy / Point / Linear
    AddressU = Clamp; // Wrap / Clamp / Mirror / Border / MirrorOnce
    AddressV = Clamp; // Wrap / Clamp / Mirror / Border / MirrorOnce
    BorderColor = 00000000; // Used only with Border edges (optional)
};

SamplerState linear_wrap
{
    Filter = Linear;
    AddressU = Wrap;
    AddressV = Wrap;
};

SamplerState point_clamp
{
    Filter = Point;
    AddressU = Clamp;
    AddressV = Clamp;
};

// Data type of the input of the vertex shader
struct vertex_data
{
    float4 pos : POSITION; // Homogeneous space coordinates XYZW
    float2 uv : TEXCOORD0; // UV coordinates in the source picture
};

// Data type of the output returned by the vertex shader, and used as input 
// for the pixel shader after interpolation for each pixel
struct pixel_data
{
    float4 pos : POSITION; // Homogeneous screen coordinates XYZW
    float2 uv : TEXCOORD0; // UV coordinates in the source picture
};

// Vertex shader used to compute position of rendered pixels and pass UV
// Essentially an identity function
pixel_data vertex_shader_auviz(vertex_data vertex)
{
    pixel_data pixel;
    pixel.pos = mul(float4(vertex.pos.xyz, 1.0), ViewProj);
    pixel.uv = vertex.uv;
    return pixel;
}

// Pixel shader used to compute an RGBA color at a given pixel position
float4 pixel_shader_auviz(pixel_data pixel) : TARGET
{
    float4 source_sample = image.Sample(linear_clamp, pixel.uv);
    
    // Change pixel colour based on max_sample_max.
    float new_red = source_sample.r + movement_strength * max_sample_max;
    float new_green = source_sample.g * movement_strength * max_sample_max;
    float new_blue = source_sample.b;
   

    return float4(new_red, new_green, new_blue, source_sample.a);
    //return float4(pixel.uv, 1.0, 1.0);
    //return float4(height, 1.0, 0.0, 1.0);

}

technique Draw
{
    pass
    {
        vertex_shader = vertex_shader_auviz(vertex);
        pixel_shader = pixel_shader_auviz(pixel);
    }
}