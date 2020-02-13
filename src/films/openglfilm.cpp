
/*
    pbrt source code is Copyright(c) 1998-2016
                        Matt Pharr, Greg Humphreys, and Wenzel Jakob.

    This file is part of pbrt.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:

    - Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    - Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
    IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
    TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
    PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
    HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
    LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 */

// core/film.cpp*
#include "openglfilm.h"

#include "imageio.h"
#include "paramset.h"
#include "stats.h"

namespace pbrt {

OpenGLFilm::OpenGLFilm(const Point2i &resolution, const Bounds2f &cropWindow,
                       std::unique_ptr<Filter> filter, Float diagonal,
                       const std::string &filename, Float scale,
                       Float maxSampleLuminance)
    : Film(resolution, cropWindow, std::move(filter), diagonal, filename, scale,
           maxSampleLuminance) {
    InitOpenGL(resolution.x, resolution.y);
    InitRenderData();
}

void OpenGLFilm::InitOpenGL(int width, int height) {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window = glfwCreateWindow(width, height, "PBRT-OPENGL", NULL, NULL);
    if (window == NULL) {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return;
    }
    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return;
    }

    glViewport(0, 0, width, height);
}

void OpenGLFilm::InitRenderData() {
    const char *vertexShaderSource =
        "#version 330 core\n"
        "layout (location = 0) in vec3 aPos;\n"
        "layout (location = 1) in vec2 aTexCoords;\n"
        "out vec2 TexCoords;\n"
        "void main()\n"
        "{\n"
        "   TexCoords = aTexCoords;\n"
        "   gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);\n"
        "}\0";
    const char *fragmentShaderSource =
        "#version 330 core\n"
        "out vec3 FragColor;\n"
        "uniform sampler2D diffuse;\n"
        "in vec2 TexCoords;\n"
        "void main()\n"
        "{\n"
        "   FragColor = texture(diffuse, TexCoords).rgb;\n"
        "}\n\0";

    // build and compile our shader program
    // ------------------------------------
    // vertex shader
    int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);
    // check for shader compile errors
    int success;
    char infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n"
                  << infoLog << std::endl;
    }
    // fragment shader
    int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);
    // check for shader compile errors
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n"
                  << infoLog << std::endl;
    }
    // link shaders
    int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    // check for linking errors
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n"
                  << infoLog << std::endl;
    }
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    float vertices[] = {
        1.0f,  1.0f,  0.0f, 1.0f, 1.0f,  // top right
        1.0f,  -1.0f, 0.0f, 1.0f, 0.0f,  // bottom right
        -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,  // bottom left
        -1.0f, 1.0f,  0.0f, 0.0f, 1.0f   // top left
    };
    unsigned int indices[] = {
        // note that we start from 0!
        0, 1, 3,  // first Triangle
        1, 2, 3   // second Triangle
    };
    unsigned int VBO, VAO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);
    // bind the Vertex Array Object first, then bind and set vertex buffer(s),
    // and then configure vertex attributes(s).
    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
                 GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    data = new unsigned char[fullResolution.x * fullResolution.y * 3];
    for (int i = 0; i < fullResolution.x; i++) {
        for (int j = 0; j < fullResolution.y; j++) {
            data[(i + j * fullResolution.x) * 3 + 0] =
                255 * i / fullResolution.x;
            data[(i + j * fullResolution.x) * 3 + 1] =
                255 * j / fullResolution.y;
            data[(i + j * fullResolution.x) * 3 + 2] = 0;
        }
    }

    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, fullResolution.x, fullResolution.y,
                 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // note that this is allowed, the call to glVertexAttribPointer registered
    // VBO as the vertex attribute's bound vertex buffer object so afterwards we
    // can safely unbind
    glUseProgram(shaderProgram);
    glUniform1i(glGetUniformLocation(shaderProgram, "diffuse"), 0);
}

void OpenGLFilm::FillRenderData(Float splatScale) {
    int offset = 0;
    for (Point2i p : croppedPixelBounds) {
        float rgb[3];
        // Convert pixel XYZ color to RGB
        Pixel &pixel = GetPixel(p);
        XYZToRGB(pixel.xyz, rgb);

        // Normalize pixel with weight sum
        Float filterWeightSum = pixel.filterWeightSum;
        if (filterWeightSum != 0) {
            Float invWt = (Float)1 / filterWeightSum;
            rgb[0] = std::max((Float)0, rgb[0] * invWt);
            rgb[1] = std::max((Float)0, rgb[1] * invWt);
            rgb[2] = std::max((Float)0, rgb[2] * invWt);
        }

        // Add splat value at pixel
        Float splatRGB[3];
        Float splatXYZ[3] = {pixel.splatXYZ[0], pixel.splatXYZ[1],
                             pixel.splatXYZ[2]};
        XYZToRGB(splatXYZ, splatRGB);
        rgb[0] += splatScale * splatRGB[0];
        rgb[1] += splatScale * splatRGB[1];
        rgb[2] += splatScale * splatRGB[2];

        // Scale pixel value by _scale_
        rgb[0] *= scale;
        rgb[1] *= scale;
        rgb[2] *= scale;
#define TO_BYTE(v) (uint8_t) Clamp(255.f * GammaCorrect(v) + 0.5f, 0.f, 255.f)
        data[3 * offset] = TO_BYTE(rgb[0]);
        data[3 * offset + 1] = TO_BYTE(rgb[1]);
        data[3 * offset + 2] = TO_BYTE(rgb[2]);
#undef TO_BYTE

        ++offset;
    }
}

void OpenGLFilm::Render() {
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, fullResolution.x, fullResolution.y,
                 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glClearColor(1.0f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glBindTexture(GL_TEXTURE_2D, textureID);

    // glDrawArrays(GL_TRIANGLES, 0, 6);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glfwSwapBuffers(window);
    glfwPollEvents();
}

void OpenGLFilm::ShowImage() {
    std::lock_guard<std::mutex> lock(mutex);
    FillRenderData();
    Render();
}

OpenGLFilm *CreateOpenGLFilm(const ParamSet &params,
                             std::unique_ptr<Filter> filter) {
    std::string filename;
    if (PbrtOptions.imageFile != "") {
        filename = PbrtOptions.imageFile;
        std::string paramsFilename = params.FindOneString("filename", "");
        if (paramsFilename != "")
            Warning(
                "Output filename supplied on command line, \"%s\" is "
                "overriding "
                "filename provided in scene description file, \"%s\".",
                PbrtOptions.imageFile.c_str(), paramsFilename.c_str());
    } else
        filename = params.FindOneString("filename", "pbrt.exr");

    int xres = params.FindOneInt("xresolution", 1280);
    int yres = params.FindOneInt("yresolution", 720);
    if (PbrtOptions.quickRender) xres = std::max(1, xres / 4);
    if (PbrtOptions.quickRender) yres = std::max(1, yres / 4);
    Bounds2f crop;
    int cwi;
    const Float *cr = params.FindFloat("cropwindow", &cwi);
    if (cr && cwi == 4) {
        crop.pMin.x = Clamp(std::min(cr[0], cr[1]), 0.f, 1.f);
        crop.pMax.x = Clamp(std::max(cr[0], cr[1]), 0.f, 1.f);
        crop.pMin.y = Clamp(std::min(cr[2], cr[3]), 0.f, 1.f);
        crop.pMax.y = Clamp(std::max(cr[2], cr[3]), 0.f, 1.f);
    } else if (cr)
        Error("%d values supplied for \"cropwindow\". Expected 4.", cwi);
    else
        crop = Bounds2f(Point2f(Clamp(PbrtOptions.cropWindow[0][0], 0, 1),
                                Clamp(PbrtOptions.cropWindow[1][0], 0, 1)),
                        Point2f(Clamp(PbrtOptions.cropWindow[0][1], 0, 1),
                                Clamp(PbrtOptions.cropWindow[1][1], 0, 1)));

    Float scale = params.FindOneFloat("scale", 1.);
    Float diagonal = params.FindOneFloat("diagonal", 35.);
    Float maxSampleLuminance =
        params.FindOneFloat("maxsampleluminance", Infinity);
    return new OpenGLFilm(Point2i(xres, yres), crop, std::move(filter),
                          diagonal, filename, scale, maxSampleLuminance);
}

}  // namespace pbrt
