#version 330 core
in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D y_tex;
uniform sampler2D u_tex;
uniform sampler2D v_tex;

void main()
{
    float y = texture(y_tex, TexCoord).r;
    float u = texture(u_tex, TexCoord).r - 0.5;
    float v = texture(v_tex, TexCoord).r - 0.5;

    // BT.601 limited range YUV → RGB
    float r = y + 1.402   * v;
    float g = y - 0.34414 * u - 0.71414 * v;
    float b = y + 1.772   * u;

    FragColor = vec4(clamp(r, 0.0, 1.0), clamp(g, 0.0, 1.0), clamp(b, 0.0, 1.0), 1.0);
}
