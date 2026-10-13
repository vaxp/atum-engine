#include "atum/hwaccel/opengl/gl_renderer.hpp"

#include <vector>
#include <iostream>

namespace atum::gl {

GlRenderer::GlRenderer(const EglContext& context) noexcept
    : context_(&context) {}

GlRenderer::~GlRenderer() {
    reset();
}

GlRenderer::GlRenderer(GlRenderer&& other) noexcept {
    *this = std::move(other);
}

GlRenderer& GlRenderer::operator=(GlRenderer&& other) noexcept {
    if (this != &other) {
        reset();

        context_ = other.context_;
        program_ = other.program_;
        vao_ = other.vao_;
        vbo_ = other.vbo_;
        u_texture_loc_ = other.u_texture_loc_;
        u_color_conv_loc_ = other.u_color_conv_loc_;
        u_color_range_loc_ = other.u_color_range_loc_;

        other.context_ = nullptr;
        other.program_ = 0;
        other.vao_ = 0;
        other.vbo_ = 0;
        other.u_texture_loc_ = -1;
        other.u_color_conv_loc_ = -1;
        other.u_color_range_loc_ = -1;
    }
    return *this;
}

void GlRenderer::reset() noexcept {
    if (!context_ || !context_->is_valid()) {
        program_ = 0;
        vao_ = 0;
        vbo_ = 0;
        return;
    }

    EglContext::Scope scope(*context_);
    const auto& disp = context_->dispatcher();

    if (vbo_ != 0 && disp.glDeleteBuffers) {
        disp.glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (vao_ != 0 && disp.glDeleteVertexArrays) {
        disp.glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    if (program_ != 0 && disp.glDeleteProgram) {
        disp.glDeleteProgram(program_);
        program_ = 0;
    }

    u_texture_loc_ = -1;
    u_color_conv_loc_ = -1;
    u_color_range_loc_ = -1;
}

MediaResult<void> GlRenderer::init() {
    if (!context_ || !context_->is_valid()) {
        return MediaError::InvalidState;
    }

    EglContext::Scope scope(*context_);
    const auto& disp = context_->dispatcher();

    if (!disp.glCreateShader || !disp.glCompileShader || !disp.glCreateProgram || !disp.glLinkProgram) {
        return MediaError::HardwareError;
    }

    // Clean-room cross-compatible GLSL Shaders
    const char* vs_source = R"(#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vTexCoord;

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

    const char* fs_source = R"(#version 130
in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uTexture;
uniform int uColorConversion;
uniform int uColorRange;

void main() {
    vec4 col = texture(uTexture, vTexCoord);
    FragColor = col;
}
)";

    // Fallback shader strings for older environments
    const char* vs_source_fallback = R"(
attribute vec2 aPos;
attribute vec2 aTexCoord;
varying vec2 vTexCoord;

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

    const char* fs_source_fallback = R"(
varying vec2 vTexCoord;
uniform sampler2D uTexture;

void main() {
    gl_FragColor = texture2D(uTexture, vTexCoord);
}
)";

    auto compile_shader = [&](GLenum type, const char* src, const char* fallback_src) -> GLuint {
        GLuint shader = disp.glCreateShader(type);
        disp.glShaderSource(shader, 1, &src, nullptr);
        disp.glCompileShader(shader);

        GLint compiled = 0;
        disp.glGetShaderiv(shader, GL_COMPILE_STATUS_VAL, &compiled);
        if (!compiled && fallback_src) {
            disp.glShaderSource(shader, 1, &fallback_src, nullptr);
            disp.glCompileShader(shader);
            disp.glGetShaderiv(shader, GL_COMPILE_STATUS_VAL, &compiled);
        }
        if (!compiled) {
            disp.glDeleteShader(shader);
            return 0;
        }
        return shader;
    };

    GLuint vs = compile_shader(GL_VERTEX_SHADER_VAL, vs_source, vs_source_fallback);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER_VAL, fs_source, fs_source_fallback);

    if (vs == 0 || fs == 0) {
        if (vs) disp.glDeleteShader(vs);
        if (fs) disp.glDeleteShader(fs);
        return MediaError::HardwareError;
    }

    program_ = disp.glCreateProgram();
    disp.glAttachShader(program_, vs);
    disp.glAttachShader(program_, fs);
    disp.glLinkProgram(program_);

    disp.glDeleteShader(vs);
    disp.glDeleteShader(fs);

    GLint linked = 0;
    disp.glGetProgramiv(program_, GL_LINK_STATUS_VAL, &linked);
    if (!linked) {
        disp.glDeleteProgram(program_);
        program_ = 0;
        return MediaError::HardwareError;
    }

    u_texture_loc_ = disp.glGetUniformLocation(program_, "uTexture");
    u_color_conv_loc_ = disp.glGetUniformLocation(program_, "uColorConversion");
    u_color_range_loc_ = disp.glGetUniformLocation(program_, "uColorRange");

    // Setup geometry (Fullscreen Quad)
    const float quad_vertices[] = {
        // X,      Y,       U,    V
        -1.0f, -1.0f,    0.0f, 0.0f,
         1.0f, -1.0f,    1.0f, 0.0f,
         1.0f,  1.0f,    1.0f, 1.0f,

        -1.0f, -1.0f,    0.0f, 0.0f,
         1.0f,  1.0f,    1.0f, 1.0f,
        -1.0f,  1.0f,    0.0f, 1.0f
    };

    if (disp.glGenVertexArrays) {
        disp.glGenVertexArrays(1, &vao_);
        disp.glBindVertexArray(vao_);
    }

    if (disp.glGenBuffers) {
        disp.glGenBuffers(1, &vbo_);
        disp.glBindBuffer(GL_ARRAY_BUFFER_VAL, vbo_);
        disp.glBufferData(GL_ARRAY_BUFFER_VAL, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW_VAL);

        disp.glEnableVertexAttribArray(0);
        disp.glVertexAttribPointer(0, 2, GL_FLOAT_VAL, GL_FALSE_VAL, 4 * sizeof(float), reinterpret_cast<void*>(0));

        disp.glEnableVertexAttribArray(1);
        disp.glVertexAttribPointer(1, 2, GL_FLOAT_VAL, GL_FALSE_VAL, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));

        if (disp.glBindVertexArray) {
            disp.glBindVertexArray(0);
        }
        disp.glBindBuffer(GL_ARRAY_BUFFER_VAL, 0);
    }

    return MediaResult<void>::ok();
}

MediaResult<void> GlRenderer::render_to_fbo(
    const GlTexture& src,
    GLuint dst_fbo,
    uint32_t dst_width,
    uint32_t dst_height,
    ColorSpace color_space,
    ColorRange color_range
) {
    if (!is_initialized()) {
        auto init_res = init();
        if (!init_res.is_ok()) {
            return init_res;
        }
    }

    if (!src.is_valid()) {
        return MediaError::InvalidData;
    }

    EglContext::Scope scope(*context_);
    const auto& disp = context_->dispatcher();

    disp.glBindFramebuffer(GL_FRAMEBUFFER_VAL, dst_fbo);
    disp.glViewport(0, 0, static_cast<GLsizei>(dst_width), static_cast<GLsizei>(dst_height));

    disp.glUseProgram(program_);

    // Bind texture to unit 0
    if (disp.glActiveTexture) {
        disp.glActiveTexture(0x84C0); // GL_TEXTURE0
    }
    disp.glBindTexture(src.target(), src.id());

    if (u_texture_loc_ >= 0) {
        disp.glUniform1i(u_texture_loc_, 0);
    }
    if (u_color_conv_loc_ >= 0) {
        disp.glUniform1i(u_color_conv_loc_, color_space == ColorSpace::BT709 ? 1 : 0);
    }
    if (u_color_range_loc_ >= 0) {
        disp.glUniform1i(u_color_range_loc_, color_range == ColorRange::Studio ? 1 : 0);
    }

    if (disp.glBindVertexArray && vao_ != 0) {
        disp.glBindVertexArray(vao_);
        disp.glDrawArrays(GL_TRIANGLES_VAL, 0, 6);
        disp.glBindVertexArray(0);
    } else if (vbo_ != 0) {
        disp.glBindBuffer(GL_ARRAY_BUFFER_VAL, vbo_);
        disp.glEnableVertexAttribArray(0);
        disp.glVertexAttribPointer(0, 2, GL_FLOAT_VAL, GL_FALSE_VAL, 4 * sizeof(float), reinterpret_cast<void*>(0));
        disp.glEnableVertexAttribArray(1);
        disp.glVertexAttribPointer(1, 2, GL_FLOAT_VAL, GL_FALSE_VAL, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
        disp.glDrawArrays(GL_TRIANGLES_VAL, 0, 6);
        disp.glBindBuffer(GL_ARRAY_BUFFER_VAL, 0);
    }

    disp.glBindTexture(src.target(), 0);
    disp.glUseProgram(0);
    disp.glBindFramebuffer(GL_FRAMEBUFFER_VAL, 0);

    return MediaResult<void>::ok();
}

MediaResult<GlTexture> GlRenderer::render_to_texture(
    const GlTexture& src,
    uint32_t dst_width,
    uint32_t dst_height,
    ColorSpace color_space,
    ColorRange color_range
) {
    if (!context_ || !context_->is_valid()) {
        return MediaError::InvalidState;
    }

    EglContext::Scope scope(*context_);
    const auto& disp = context_->dispatcher();

    GLuint dst_tex = 0;
    disp.glGenTextures(1, &dst_tex);
    disp.glBindTexture(GL_TEXTURE_2D_VAL, dst_tex);
    disp.glTexImage2D(
        GL_TEXTURE_2D_VAL,
        0,
        static_cast<GLint>(GL_RGBA8_VAL),
        static_cast<GLsizei>(dst_width),
        static_cast<GLsizei>(dst_height),
        0,
        GL_RGBA_VAL,
        GL_UNSIGNED_BYTE_VAL,
        nullptr
    );
    disp.glTexParameteri(GL_TEXTURE_2D_VAL, GL_TEXTURE_MIN_FILTER_VAL, GL_LINEAR_VAL);
    disp.glTexParameteri(GL_TEXTURE_2D_VAL, GL_TEXTURE_MAG_FILTER_VAL, GL_LINEAR_VAL);
    disp.glTexParameteri(GL_TEXTURE_2D_VAL, GL_TEXTURE_WRAP_S_VAL, GL_CLAMP_TO_EDGE_VAL);
    disp.glTexParameteri(GL_TEXTURE_2D_VAL, GL_TEXTURE_WRAP_T_VAL, GL_CLAMP_TO_EDGE_VAL);

    GLuint fbo = 0;
    disp.glGenFramebuffers(1, &fbo);
    disp.glBindFramebuffer(GL_FRAMEBUFFER_VAL, fbo);
    disp.glFramebufferTexture2D(GL_FRAMEBUFFER_VAL, GL_COLOR_ATTACHMENT0_VAL, GL_TEXTURE_2D_VAL, dst_tex, 0);

    auto render_res = render_to_fbo(src, fbo, dst_width, dst_height, color_space, color_range);

    disp.glBindFramebuffer(GL_FRAMEBUFFER_VAL, 0);
    disp.glDeleteFramebuffers(1, &fbo);
    disp.glBindTexture(GL_TEXTURE_2D_VAL, 0);

    if (!render_res.is_ok()) {
        disp.glDeleteTextures(1, &dst_tex);
        return render_res.error();
    }

    GlTexture result(*context_);
    result.texture_ = dst_tex;
    result.target_ = GL_TEXTURE_2D_VAL;
    result.width_ = dst_width;
    result.height_ = dst_height;
    result.drm_fourcc_ = DRM_FOURCC_RGBA8888;

    return result;
}

} // namespace atum::gl
