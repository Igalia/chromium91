//
// Copyright 2017 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// ProgramPipelineTest:
//   Various tests related to Program Pipeline.
//

#include "test_utils/ANGLETest.h"
#include "test_utils/gl_raii.h"

using namespace angle;

namespace
{

class ProgramPipelineTest : public ANGLETest
{
  protected:
    ProgramPipelineTest()
    {
        setWindowWidth(64);
        setWindowHeight(64);
        setConfigRedBits(8);
        setConfigGreenBits(8);
        setConfigBlueBits(8);
        setConfigAlphaBits(8);
    }
};

// Verify that program pipeline is not supported in version lower than ES31.
TEST_P(ProgramPipelineTest, GenerateProgramPipelineObject)
{
    ANGLE_SKIP_TEST_IF(!IsVulkan());

    GLuint pipeline;
    glGenProgramPipelines(1, &pipeline);
    if (getClientMajorVersion() < 3 || getClientMinorVersion() < 1)
    {
        EXPECT_GL_ERROR(GL_INVALID_OPERATION);
    }
    else
    {
        EXPECT_GL_NO_ERROR();

        glDeleteProgramPipelines(1, &pipeline);
        EXPECT_GL_NO_ERROR();
    }
}

// Verify that program pipeline errors out without GL_EXT_separate_shader_objects extension.
TEST_P(ProgramPipelineTest, GenerateProgramPipelineObjectEXT)
{
    GLuint pipeline;
    glGenProgramPipelinesEXT(1, &pipeline);
    if (!IsGLExtensionEnabled("GL_EXT_separate_shader_objects"))
    {
        EXPECT_GL_ERROR(GL_INVALID_OPERATION);
    }
    else
    {
        EXPECT_GL_NO_ERROR();

        glDeleteProgramPipelinesEXT(1, &pipeline);
        EXPECT_GL_NO_ERROR();
    }
}

class ProgramPipelineTest31 : public ProgramPipelineTest
{
  protected:
    ~ProgramPipelineTest31()
    {
        glDeleteProgram(mVertProg);
        glDeleteProgram(mFragProg);
        glDeleteProgramPipelines(1, &mPipeline);
    }

    void bindProgramPipeline(const GLchar *vertString, const GLchar *fragString);
    void drawQuadWithPPO(const std::string &positionAttribName,
                         const GLfloat positionAttribZ,
                         const GLfloat positionAttribXYScale);

    GLuint mVertProg;
    GLuint mFragProg;
    GLuint mPipeline;
};

void ProgramPipelineTest31::bindProgramPipeline(const GLchar *vertString, const GLchar *fragString)
{
    mVertProg = glCreateShaderProgramv(GL_VERTEX_SHADER, 1, &vertString);
    ASSERT_NE(mVertProg, 0u);
    mFragProg = glCreateShaderProgramv(GL_FRAGMENT_SHADER, 1, &fragString);
    ASSERT_NE(mFragProg, 0u);

    // Generate a program pipeline and attach the programs to their respective stages
    glGenProgramPipelines(1, &mPipeline);
    EXPECT_GL_NO_ERROR();
    glUseProgramStages(mPipeline, GL_VERTEX_SHADER_BIT, mVertProg);
    EXPECT_GL_NO_ERROR();
    glUseProgramStages(mPipeline, GL_FRAGMENT_SHADER_BIT, mFragProg);
    EXPECT_GL_NO_ERROR();
    glBindProgramPipeline(mPipeline);
    EXPECT_GL_NO_ERROR();
}

// Test generate or delete program pipeline.
TEST_P(ProgramPipelineTest31, GenOrDeleteProgramPipelineTest)
{
    ANGLE_SKIP_TEST_IF(!IsVulkan());

    GLuint pipeline;
    glGenProgramPipelines(-1, &pipeline);
    EXPECT_GL_ERROR(GL_INVALID_VALUE);
    glGenProgramPipelines(0, &pipeline);
    EXPECT_GL_NO_ERROR();

    glDeleteProgramPipelines(-1, &pipeline);
    EXPECT_GL_ERROR(GL_INVALID_VALUE);
    glDeleteProgramPipelines(0, &pipeline);
    EXPECT_GL_NO_ERROR();
}

// Test BindProgramPipeline.
TEST_P(ProgramPipelineTest31, BindProgramPipelineTest)
{
    ANGLE_SKIP_TEST_IF(!IsVulkan());

    glBindProgramPipeline(0);
    EXPECT_GL_NO_ERROR();

    glBindProgramPipeline(2);
    EXPECT_GL_ERROR(GL_INVALID_OPERATION);

    GLuint pipeline;
    glGenProgramPipelines(1, &pipeline);
    glBindProgramPipeline(pipeline);
    EXPECT_GL_NO_ERROR();

    glDeleteProgramPipelines(1, &pipeline);
    glBindProgramPipeline(pipeline);
    EXPECT_GL_ERROR(GL_INVALID_OPERATION);
}

// Test IsProgramPipeline
TEST_P(ProgramPipelineTest31, IsProgramPipelineTest)
{
    ANGLE_SKIP_TEST_IF(!IsVulkan());

    EXPECT_GL_FALSE(glIsProgramPipeline(0));
    EXPECT_GL_NO_ERROR();

    EXPECT_GL_FALSE(glIsProgramPipeline(2));
    EXPECT_GL_NO_ERROR();

    GLuint pipeline;
    glGenProgramPipelines(1, &pipeline);
    glBindProgramPipeline(pipeline);
    EXPECT_GL_TRUE(glIsProgramPipeline(pipeline));
    EXPECT_GL_NO_ERROR();

    glBindProgramPipeline(0);
    glDeleteProgramPipelines(1, &pipeline);
    EXPECT_GL_FALSE(glIsProgramPipeline(pipeline));
    EXPECT_GL_NO_ERROR();
}

// Simulates a call to glCreateShaderProgramv()
GLuint createShaderProgram(GLenum type, const GLchar *shaderString)
{
    GLShader shader(type);
    if (!shader.get())
    {
        return 0;
    }

    glShaderSource(shader, 1, &shaderString, nullptr);
    glCompileShader(shader);

    GLuint program = glCreateProgram();

    if (program)
    {
        GLint compiled;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        glProgramParameteri(program, GL_PROGRAM_SEPARABLE, GL_TRUE);
        if (compiled)
        {
            glAttachShader(program, shader);
            glLinkProgram(program);
            glDetachShader(program, shader);
        }
    }

    EXPECT_GL_NO_ERROR();

    return program;
}

void ProgramPipelineTest31::drawQuadWithPPO(const std::string &positionAttribName,
                                            const GLfloat positionAttribZ,
                                            const GLfloat positionAttribXYScale)
{
    return drawQuadPPO(mVertProg, positionAttribName, positionAttribZ, positionAttribXYScale);
}

// Test glUseProgramStages
TEST_P(ProgramPipelineTest31, UseProgramStages)
{
    ANGLE_SKIP_TEST_IF(!IsVulkan());

    // Create two separable program objects from a
    // single source string respectively (vertSrc and fragSrc)
    const GLchar *vertString = essl31_shaders::vs::Simple();
    const GLchar *fragString = essl31_shaders::fs::Red();

    mVertProg = createShaderProgram(GL_VERTEX_SHADER, vertString);
    ASSERT_NE(mVertProg, 0u);
    mFragProg = createShaderProgram(GL_FRAGMENT_SHADER, fragString);
    ASSERT_NE(mFragProg, 0u);

    // Generate a program pipeline and attach the programs to their respective stages
    GLuint pipeline;
    glGenProgramPipelines(1, &pipeline);
    EXPECT_GL_NO_ERROR();
    glUseProgramStages(pipeline, GL_VERTEX_SHADER_BIT, mVertProg);
    EXPECT_GL_NO_ERROR();
    glUseProgramStages(pipeline, GL_FRAGMENT_SHADER_BIT, mFragProg);
    EXPECT_GL_NO_ERROR();
    glBindProgramPipeline(pipeline);
    EXPECT_GL_NO_ERROR();

    drawQuadWithPPO("a_position", 0.5f, 1.0f);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);
}

// Test glUseProgramStages
TEST_P(ProgramPipelineTest31, UseCreateShaderProgramv)
{
    ANGLE_SKIP_TEST_IF(!IsVulkan());

    // Create two separable program objects from a
    // single source string respectively (vertSrc and fragSrc)
    const GLchar *vertString = essl31_shaders::vs::Simple();
    const GLchar *fragString = essl31_shaders::fs::Red();

    bindProgramPipeline(vertString, fragString);

    drawQuadWithPPO("a_position", 0.5f, 1.0f);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);
}

// Test glUniform
TEST_P(ProgramPipelineTest31, FragmentStageUniformTest)
{
    ANGLE_SKIP_TEST_IF(!IsVulkan());

    // Create two separable program objects from a
    // single source string respectively (vertSrc and fragSrc)
    const GLchar *vertString = essl31_shaders::vs::Simple();
    const GLchar *fragString = R"(#version 310 es
precision highp float;
uniform float redColorIn;
uniform float greenColorIn;
out vec4 my_FragColor;
void main()
{
    my_FragColor = vec4(redColorIn, greenColorIn, 0.0, 1.0);
})";

    bindProgramPipeline(vertString, fragString);

    // Set the output color to yellow
    GLint location = glGetUniformLocation(mFragProg, "redColorIn");
    glActiveShaderProgram(mPipeline, mFragProg);
    glUniform1f(location, 1.0);
    location = glGetUniformLocation(mFragProg, "greenColorIn");
    glActiveShaderProgram(mPipeline, mFragProg);
    glUniform1f(location, 1.0);

    drawQuadWithPPO("a_position", 0.5f, 1.0f);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::yellow);

    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);

    drawQuadWithPPO("a_position", 0.5f, 1.0f);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::yellow);

    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);

    // Set the output color to red
    location = glGetUniformLocation(mFragProg, "redColorIn");
    glActiveShaderProgram(mPipeline, mFragProg);
    glUniform1f(location, 1.0);
    location = glGetUniformLocation(mFragProg, "greenColorIn");
    glActiveShaderProgram(mPipeline, mFragProg);
    glUniform1f(location, 0.0);

    drawQuadWithPPO("a_position", 0.5f, 1.0f);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);

    glDeleteProgram(mVertProg);
    glDeleteProgram(mFragProg);
}

// Test varyings
TEST_P(ProgramPipelineTest31, ProgramPipelineVaryings)
{
    ANGLE_SKIP_TEST_IF(!IsVulkan());

    // Create two separable program objects from a
    // single source string respectively (vertSrc and fragSrc)
    const GLchar *vertString = essl31_shaders::vs::Passthrough();
    const GLchar *fragString = R"(#version 310 es
precision highp float;
in vec4 v_position;
out vec4 my_FragColor;
void main()
{
    my_FragColor = round(v_position);
})";

    bindProgramPipeline(vertString, fragString);

    drawQuadWithPPO("a_position", 0.5f, 1.0f);
    ASSERT_GL_NO_ERROR();

    int w = getWindowWidth() - 2;
    int h = getWindowHeight() - 2;

    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::black);
    EXPECT_PIXEL_COLOR_EQ(w, 0, GLColor::red);
    EXPECT_PIXEL_COLOR_EQ(0, h, GLColor::green);
    EXPECT_PIXEL_COLOR_EQ(w, h, GLColor::yellow);
}

// Creates a program pipeline with a 2D texture and renders with it.
TEST_P(ProgramPipelineTest31, DrawWith2DTexture)
{
    ANGLE_SKIP_TEST_IF(!IsVulkan());

    const GLchar *vertString = R"(#version 310 es
precision highp float;
in vec4 a_position;
out vec2 texCoord;
void main()
{
    gl_Position = a_position;
    texCoord = vec2(a_position.x, a_position.y) * 0.5 + vec2(0.5);
})";

    const GLchar *fragString = R"(#version 310 es
precision highp float;
in vec2 texCoord;
uniform sampler2D tex;
out vec4 my_FragColor;
void main()
{
    my_FragColor = texture(tex, texCoord);
})";

    std::array<GLColor, 4> colors = {
        {GLColor::red, GLColor::green, GLColor::blue, GLColor::yellow}};

    GLTexture tex;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, colors.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    bindProgramPipeline(vertString, fragString);

    drawQuadWithPPO("a_position", 0.5f, 1.0f);
    ASSERT_GL_NO_ERROR();

    int w = getWindowWidth() - 2;
    int h = getWindowHeight() - 2;

    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);
    EXPECT_PIXEL_COLOR_EQ(w, 0, GLColor::green);
    EXPECT_PIXEL_COLOR_EQ(0, h, GLColor::blue);
    EXPECT_PIXEL_COLOR_EQ(w, h, GLColor::yellow);
}

// Test modifying a shader after it has been detached from a pipeline
TEST_P(ProgramPipelineTest31, DetachAndModifyShader)
{
    ANGLE_SKIP_TEST_IF(!IsVulkan());

    const GLchar *vertString = essl31_shaders::vs::Simple();
    const GLchar *fragString = essl31_shaders::fs::Green();

    GLShader vertShader(GL_VERTEX_SHADER);
    GLShader fragShader(GL_FRAGMENT_SHADER);
    mVertProg = glCreateProgram();
    mFragProg = glCreateProgram();

    // Compile and link a separable vertex shader
    glShaderSource(vertShader, 1, &vertString, nullptr);
    glCompileShader(vertShader);
    glProgramParameteri(mVertProg, GL_PROGRAM_SEPARABLE, GL_TRUE);
    glAttachShader(mVertProg, vertShader);
    glLinkProgram(mVertProg);
    EXPECT_GL_NO_ERROR();

    // Compile and link a separable fragment shader
    glShaderSource(fragShader, 1, &fragString, nullptr);
    glCompileShader(fragShader);
    glProgramParameteri(mFragProg, GL_PROGRAM_SEPARABLE, GL_TRUE);
    glAttachShader(mFragProg, fragShader);
    glLinkProgram(mFragProg);
    EXPECT_GL_NO_ERROR();

    // Generate a program pipeline and attach the programs
    glGenProgramPipelines(1, &mPipeline);
    glUseProgramStages(mPipeline, GL_VERTEX_SHADER_BIT, mVertProg);
    glUseProgramStages(mPipeline, GL_FRAGMENT_SHADER_BIT, mFragProg);
    glBindProgramPipeline(mPipeline);
    EXPECT_GL_NO_ERROR();

    // Draw once to ensure this worked fine
    drawQuadWithPPO("a_position", 0.5f, 1.0f);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);

    // Detach the fragment shader and modify it such that it no longer fits with this pipeline
    glDetachShader(mFragProg, fragShader);

    // Add an input to the fragment shader, which will make it incompatible
    const GLchar *fragString2 = R"(#version 310 es
precision highp float;
in vec4 color;
out vec4 my_FragColor;
void main()
{
    my_FragColor = color;
})";
    glShaderSource(fragShader, 1, &fragString2, nullptr);
    glCompileShader(fragShader);

    // Link and draw with the program again, which should be fine since the shader was detached
    glLinkProgram(mFragProg);

    drawQuadWithPPO("a_position", 0.5f, 1.0f);
    ASSERT_GL_NO_ERROR();
}

// Test binding two programs that use a texture as different types
TEST_P(ProgramPipelineTest31, DifferentTextureTypes)
{
    // Only the Vulkan backend supports PPO
    ANGLE_SKIP_TEST_IF(!IsVulkan());

    // Per the OpenGL ES 3.1 spec:
    //
    // It is not allowed to have variables of different sampler types pointing to the same texture
    // image unit within a program object. This situation can only be detected at the next rendering
    // command issued which triggers shader invocations, and an INVALID_OPERATION error will then
    // be generated
    //

    // Create a vertex shader that uses the texture as 2D
    const GLchar *vertString = R"(#version 310 es
precision highp float;
in vec4 a_position;
uniform sampler2D tex2D;
layout(location = 0) out vec4 texColorOut;
layout(location = 1) out vec2 texCoordOut;
void main()
{
    gl_Position = a_position;
    vec2 texCoord = vec2(a_position.x, a_position.y) * 0.5 + vec2(0.5);
    texColorOut = textureLod(tex2D, texCoord, 0.0);
    texCoordOut = texCoord;
})";

    // Create a fragment shader that uses the texture as Cube
    const GLchar *fragString = R"(#version 310 es
precision highp float;
layout(location = 0) in vec4 texColor;
layout(location = 1) in vec2 texCoord;
uniform samplerCube texCube;
out vec4 my_FragColor;
void main()
{
    my_FragColor = texture(texCube, vec3(texCoord.x, texCoord.y, 0.0));
})";

    // Create and populate the 2D texture
    std::array<GLColor, 4> colors = {
        {GLColor::red, GLColor::green, GLColor::blue, GLColor::yellow}};
    GLTexture tex;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, colors.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Create a pipeline that uses the bad combination.  This should fail to link the pipeline.
    bindProgramPipeline(vertString, fragString);
    drawQuadWithPPO("a_position", 0.5f, 1.0f);
    ASSERT_GL_ERROR(GL_INVALID_OPERATION);

    // Update the fragment shader to correctly use 2D texture
    const GLchar *fragString2 = R"(#version 310 es
precision highp float;
layout(location = 0) in vec4 texColor;
layout(location = 1) in vec2 texCoord;
uniform sampler2D tex2D;
out vec4 my_FragColor;
void main()
{
    my_FragColor = texture(tex2D, texCoord);
})";

    // Bind the pipeline again, which should succeed.
    bindProgramPipeline(vertString, fragString2);
    drawQuadWithPPO("a_position", 0.5f, 1.0f);
    ASSERT_GL_NO_ERROR();
}

// Tests that we receive a PPO link validation error when attempting to draw with the bad PPO
TEST_P(ProgramPipelineTest31, VerifyPpoLinkErrorSignalledCorrectly)
{
    // Create pipeline that should fail link
    // Bind program
    // Draw
    // Unbind program
    // Draw  <<--- expect a link validation error here

    // Only the Vulkan backend supports PPOs
    ANGLE_SKIP_TEST_IF(!IsVulkan());

    // Create two separable program objects from a
    // single source string respectively (vertSrc and fragSrc)
    const GLchar *vertString = essl31_shaders::vs::Simple();
    const GLchar *fragString = essl31_shaders::fs::Red();
    // Create a fragment shader that takes a color input
    // This should cause the PPO link to fail, since the varyings don't match (no output from VS).
    const GLchar *fragStringBad = R"(#version 310 es
precision highp float;
layout(location = 0) in vec4 colorIn;
out vec4 my_FragColor;
void main()
{
    my_FragColor = colorIn;
})";
    bindProgramPipeline(vertString, fragStringBad);

    ANGLE_GL_PROGRAM(program, vertString, fragString);
    drawQuad(program.get(), essl1_shaders::PositionAttrib(), 0.0f, 1.0f, true);
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);

    // Draw with the PPO, which should generate an error due to the link failure.
    glUseProgram(0);
    ASSERT_GL_NO_ERROR();
    drawQuadWithPPO(essl1_shaders::PositionAttrib(), 0.5f, 1.0f);
    ASSERT_GL_ERROR(GL_INVALID_OPERATION);
}

// Tests creating two program pipelines with a common shader and a varying location mismatch.
TEST_P(ProgramPipelineTest31, VaryingLocationMismatch)
{
    // Only the Vulkan backend supports PPOs
    ANGLE_SKIP_TEST_IF(!IsVulkan());

    // http://anglebug.com/5506
    ANGLE_SKIP_TEST_IF(IsVulkan());

    // Create a fragment shader using the varying location "5".
    const char *kFS = R"(#version 310 es
precision mediump float;
layout(location = 5) in vec4 color;
out vec4 colorOut;
void main()
{
    colorOut = color;
})";

    // Create a pipeline with a vertex shader using varying location "5". Should succeed.
    const char *kVSGood = R"(#version 310 es
precision mediump float;
layout(location = 5) out vec4 color;
in vec4 position;
uniform float uniOne;
void main()
{
    gl_Position = position;
    color = vec4(0, uniOne, 0, 1);
})";

    mVertProg = glCreateShaderProgramv(GL_VERTEX_SHADER, 1, &kVSGood);
    ASSERT_NE(mVertProg, 0u);
    mFragProg = glCreateShaderProgramv(GL_FRAGMENT_SHADER, 1, &kFS);
    ASSERT_NE(mFragProg, 0u);

    // Generate a program pipeline and attach the programs to their respective stages
    glGenProgramPipelines(1, &mPipeline);
    glUseProgramStages(mPipeline, GL_VERTEX_SHADER_BIT, mVertProg);
    glUseProgramStages(mPipeline, GL_FRAGMENT_SHADER_BIT, mFragProg);
    glBindProgramPipeline(mPipeline);
    ASSERT_GL_NO_ERROR();

    GLint location = glGetUniformLocation(mVertProg, "uniOne");
    ASSERT_NE(-1, location);
    glActiveShaderProgram(mPipeline, mVertProg);
    glUniform1f(location, 1.0);
    ASSERT_GL_NO_ERROR();

    drawQuadWithPPO("position", 0.5f, 1.0f);
    ASSERT_GL_NO_ERROR();

    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);

    // Create a pipeline with a vertex shader using varying location "3". Should fail.
    const char *kVSBad = R"(#version 310 es
precision mediump float;
layout(location = 3) out vec4 color;
in vec4 position;
uniform float uniOne;
void main()
{
    gl_Position = position;
    color = vec4(0, uniOne, 0, 1);
})";

    glDeleteProgram(mVertProg);
    mVertProg = glCreateShaderProgramv(GL_VERTEX_SHADER, 1, &kVSBad);
    ASSERT_NE(mVertProg, 0u);

    glUseProgramStages(mPipeline, GL_VERTEX_SHADER_BIT, mVertProg);
    ASSERT_GL_NO_ERROR();

    drawQuadWithPPO("position", 0.5f, 1.0f);
    EXPECT_GL_ERROR(GL_INVALID_OPERATION);
}

// Test that a shader IO block varying with separable program links
// successfully.
TEST_P(ProgramPipelineTest31, VaryingIOBlockSeparableProgram)
{
    // Only the Vulkan backend supports PPOs
    ANGLE_SKIP_TEST_IF(!IsVulkan());
    ANGLE_SKIP_TEST_IF(!IsGLExtensionEnabled("GL_EXT_shader_io_blocks"));

    constexpr char kVS[] =
        R"(#version 310 es
        #extension GL_EXT_shader_io_blocks : require

        precision highp float;
        in vec4 inputAttribute;
        out Block_inout { vec4 value; } user_out;

        void main()
        {
            gl_Position    = inputAttribute;
            user_out.value = vec4(4.0, 5.0, 6.0, 7.0);
        })";

    constexpr char kFS[] =
        R"(#version 310 es
        #extension GL_EXT_shader_io_blocks : require

        precision highp float;
        layout(location = 0) out mediump vec4 color;
        in Block_inout { vec4 value; } user_in;

        void main()
        {
            color = vec4(1, 0, 0, 1);
        })";

    bindProgramPipeline(kVS, kFS);

    drawQuadWithPPO("inputAttribute", 0.5f, 1.0f);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);
}

// Test modifying a shader and re-linking it updates the PPO too
TEST_P(ProgramPipelineTest31, ModifyAndRelinkShader)
{
    ANGLE_SKIP_TEST_IF(!IsVulkan());

    const GLchar *vertString      = essl31_shaders::vs::Simple();
    const GLchar *fragStringGreen = essl31_shaders::fs::Green();
    const GLchar *fragStringRed   = essl31_shaders::fs::Red();

    GLShader vertShader(GL_VERTEX_SHADER);
    GLShader fragShader(GL_FRAGMENT_SHADER);
    mVertProg = glCreateProgram();
    mFragProg = glCreateProgram();

    // Compile and link a separable vertex shader
    glShaderSource(vertShader, 1, &vertString, nullptr);
    glCompileShader(vertShader);
    glProgramParameteri(mVertProg, GL_PROGRAM_SEPARABLE, GL_TRUE);
    glAttachShader(mVertProg, vertShader);
    glLinkProgram(mVertProg);
    EXPECT_GL_NO_ERROR();

    // Compile and link a separable fragment shader
    glShaderSource(fragShader, 1, &fragStringGreen, nullptr);
    glCompileShader(fragShader);
    glProgramParameteri(mFragProg, GL_PROGRAM_SEPARABLE, GL_TRUE);
    glAttachShader(mFragProg, fragShader);
    glLinkProgram(mFragProg);
    EXPECT_GL_NO_ERROR();

    // Generate a program pipeline and attach the programs
    glGenProgramPipelines(1, &mPipeline);
    glUseProgramStages(mPipeline, GL_VERTEX_SHADER_BIT, mVertProg);
    glUseProgramStages(mPipeline, GL_FRAGMENT_SHADER_BIT, mFragProg);
    glBindProgramPipeline(mPipeline);
    EXPECT_GL_NO_ERROR();

    // Draw once to ensure this worked fine
    drawQuadWithPPO("a_position", 0.5f, 1.0f);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::green);

    // Detach the fragment shader and modify it such that it no longer fits with this pipeline
    glDetachShader(mFragProg, fragShader);

    // Modify the FS and re-link it
    glShaderSource(fragShader, 1, &fragStringRed, nullptr);
    glCompileShader(fragShader);
    glProgramParameteri(mFragProg, GL_PROGRAM_SEPARABLE, GL_TRUE);
    glAttachShader(mFragProg, fragShader);
    glLinkProgram(mFragProg);
    EXPECT_GL_NO_ERROR();

    // Draw with the PPO again and verify it's now red
    drawQuadWithPPO("a_position", 0.5f, 1.0f);
    ASSERT_GL_NO_ERROR();
    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);
}

// Test that a PPO can be used when the attached shader programs are created with glProgramBinary().
// This validates the necessary programs' information is serialized/deserialized so they can be
// linked by the PPO during glDrawArrays.
TEST_P(ProgramPipelineTest31, ProgramBinary)
{
    ANGLE_SKIP_TEST_IF(!IsVulkan());

    const GLchar *vertString = R"(#version 310 es
precision highp float;
in vec4 a_position;
out vec2 texCoord;
void main()
{
    gl_Position = a_position;
    texCoord = vec2(a_position.x, a_position.y) * 0.5 + vec2(0.5);
})";

    const GLchar *fragString = R"(#version 310 es
precision highp float;
in vec2 texCoord;
uniform sampler2D tex;
out vec4 my_FragColor;
void main()
{
    my_FragColor = texture(tex, texCoord);
})";

    std::array<GLColor, 4> colors = {
        {GLColor::red, GLColor::green, GLColor::blue, GLColor::yellow}};

    GLTexture tex;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, colors.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    mVertProg = glCreateShaderProgramv(GL_VERTEX_SHADER, 1, &vertString);
    ASSERT_NE(mVertProg, 0u);
    mFragProg = glCreateShaderProgramv(GL_FRAGMENT_SHADER, 1, &fragString);
    ASSERT_NE(mFragProg, 0u);

    // Save the VS program binary out
    std::vector<uint8_t> vsBinary(0);
    GLint vsProgramLength = 0;
    GLint vsWrittenLength = 0;
    GLenum vsBinaryFormat = 0;
    glGetProgramiv(mVertProg, GL_PROGRAM_BINARY_LENGTH, &vsProgramLength);
    ASSERT_GL_NO_ERROR();
    vsBinary.resize(vsProgramLength);
    glGetProgramBinary(mVertProg, vsProgramLength, &vsWrittenLength, &vsBinaryFormat,
                       vsBinary.data());
    ASSERT_GL_NO_ERROR();

    // Save the FS program binary out
    std::vector<uint8_t> fsBinary(0);
    GLint fsProgramLength = 0;
    GLint fsWrittenLength = 0;
    GLenum fsBinaryFormat = 0;
    glGetProgramiv(mFragProg, GL_PROGRAM_BINARY_LENGTH, &fsProgramLength);
    ASSERT_GL_NO_ERROR();
    fsBinary.resize(fsProgramLength);
    glGetProgramBinary(mFragProg, fsProgramLength, &fsWrittenLength, &fsBinaryFormat,
                       fsBinary.data());
    ASSERT_GL_NO_ERROR();

    mVertProg = glCreateProgram();
    glProgramBinary(mVertProg, vsBinaryFormat, vsBinary.data(), vsWrittenLength);
    mFragProg = glCreateProgram();
    glProgramBinary(mFragProg, fsBinaryFormat, fsBinary.data(), fsWrittenLength);

    // Generate a program pipeline and attach the programs to their respective stages
    glGenProgramPipelines(1, &mPipeline);
    EXPECT_GL_NO_ERROR();
    glUseProgramStages(mPipeline, GL_VERTEX_SHADER_BIT, mVertProg);
    EXPECT_GL_NO_ERROR();
    glUseProgramStages(mPipeline, GL_FRAGMENT_SHADER_BIT, mFragProg);
    EXPECT_GL_NO_ERROR();
    glBindProgramPipeline(mPipeline);
    EXPECT_GL_NO_ERROR();

    drawQuadWithPPO("a_position", 0.5f, 1.0f);
    ASSERT_GL_NO_ERROR();

    int w = getWindowWidth() - 2;
    int h = getWindowHeight() - 2;

    EXPECT_PIXEL_COLOR_EQ(0, 0, GLColor::red);
    EXPECT_PIXEL_COLOR_EQ(w, 0, GLColor::green);
    EXPECT_PIXEL_COLOR_EQ(0, h, GLColor::blue);
    EXPECT_PIXEL_COLOR_EQ(w, h, GLColor::yellow);
}

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(ProgramPipelineTest);
ANGLE_INSTANTIATE_TEST_ES3_AND_ES31(ProgramPipelineTest);

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(ProgramPipelineTest31);
ANGLE_INSTANTIATE_TEST_ES31(ProgramPipelineTest31);

}  // namespace
