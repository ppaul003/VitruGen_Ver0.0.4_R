#include <GL/glew.h>

#include <stdio.h>
#include <vector>
#include <assert.h>
#include <math.h>
#include <memory.h>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <filesystem>

#include "renderer_Euclid.h"
#include "render_utils.h"

using namespace std;
using namespace glm;

EuclidRenderer::EuclidRenderer() :
    m_bInitialized(false),
    m_rad(0),
    m_pos(0),
    m_radCapacity(0),
    m_numParticles(0),
    m_pointSize(1.0f),
    m_particleRadius(0.125f * 0.5f),
    m_fov(60.0f),
    m_window_w(0),
    m_window_h(0),
    m_program0(0),
    m_program1(0),
    m_meshProgram(0),
    m_vbo(0),
    m_radVBO(0),
    m_colorVBO(0) {


    _initGL();
    _initialize();
}

EuclidRenderer::~EuclidRenderer() {
    m_pos = 0;

    delete[] m_rad;
    m_rad = nullptr;

    // Deletes the OBJ VBO and clears the associated CPU data.
    //clearParticleMeshOBJ();

    if (m_program0) {
        glDeleteProgram(m_program0);
        m_program0 = 0;
    }

    if (m_program1) {
        glDeleteProgram(m_program1);
        m_program1 = 0;
    }

    if (m_meshProgram) {
        glDeleteProgram(m_meshProgram);
        m_meshProgram = 0;
    }

    if (m_particlePosVBO) {
        glDeleteBuffers(1, &m_particlePosVBO);

        m_particlePosVBO = 0;
    }

    if (m_particleRadVBO) {

        glDeleteBuffers(1, &m_particleRadVBO);
        m_particleRadVBO = 0;
    }

    if (m_particleColorVBO) {
        glDeleteBuffers(1, &m_particleColorVBO);
        m_particleColorVBO = 0;
    }
}

void EuclidRenderer::drawGridBoundary(const UniformGrid& grid) {

    const vec3 min = grid.origin;

    const vec3 max = grid.origin +
        vec3(grid.dimensions) * grid.cellSize;

    glUseProgram(0);
    glDisable(GL_TEXTURE_2D);

    glEnable(GL_BLEND);

    glBlendFunc(
        GL_SRC_ALPHA, 
        GL_ONE_MINUS_SRC_ALPHA
    );

    glLineWidth(1.5f);
    glColor4f(1.0f, 1.0f, 1.0f, 0.55f);

    glBegin(GL_LINES);

    // Bottom rectangle
    glVertex3f(min.x, min.y, min.z);
    glVertex3f(max.x, min.y, min.z);

    glVertex3f(max.x, min.y, min.z);
    glVertex3f(max.x, min.y, max.z);

    glVertex3f(max.x, min.y, max.z);
    glVertex3f(min.x, min.y, max.z);

    glVertex3f(min.x, min.y, max.z);
    glVertex3f(min.x, min.y, min.z);

    // Top rectangle
    glVertex3f(min.x, max.y, min.z);
    glVertex3f(max.x, max.y, min.z);

    glVertex3f(max.x, max.y, min.z);
    glVertex3f(max.x, max.y, max.z);

    glVertex3f(max.x, max.y, max.z);
    glVertex3f(min.x, max.y, max.z);

    glVertex3f(min.x, max.y, max.z);
    glVertex3f(min.x, max.y, min.z);

    // Verticle edges
    glVertex3f(min.x, min.y, min.z);
    glVertex3f(min.x, max.y, min.z);

    glVertex3f(max.x, min.y, min.z);
    glVertex3f(max.x, max.y, min.z);

    glVertex3f(max.x, min.y, max.z);
    glVertex3f(max.x, max.y, max.z);

    glVertex3f(min.x, min.y, max.z);
    glVertex3f(min.x, max.y, max.z);

    glEnd();
    glLineWidth(1.0f);
    glDisable(GL_BLEND);
}

void EuclidRenderer::drawGridAxes(const UniformGrid& grid) {

    const vec3 extent =
        vec3(grid.dimensions) * grid.cellSize;

    const float axisLength =
        0.20f * std::max({ extent.x, extent.y, extent.z });

    glUseProgram(0);
    glLineWidth(2.0f);
    
    glBegin(GL_LINES);

    // +X
    glColor4f(1.0f, 0.0f, 0.0f, 1.0f);

    glVertex3f(0.0f, 0.0f, 0.0f);
    glVertex3f(axisLength, 0.0f, 0.0f);
    
    // +Y
    glColor4f(0.0f, 1.0f, 0.0f, 1.0f);

    glVertex3f(0.0f, 0.0f, 0.0f);
    glVertex3f(0.0f, axisLength, 0.0f);

    // +Z
    glColor4f(0.0f, 0.0f, 1.0f, 1.0f);

    glVertex3f(0.0f, 0.0f, 0.0f);
    glVertex3f(0.0f, 0.0f, axisLength);

    glEnd();
    glLineWidth(1.0f);
}

void EuclidRenderer::drawGridPlane(
    const UniformGrid& grid,
    GridPlane plane,
    float planePosition,
    bool drawMinorLines) {

    drawGridPlaneLines(
        grid,
        plane,
        planePosition,
        drawMinorLines
    );
}

void EuclidRenderer::drawGridPlaneLines(
    const UniformGrid& grid,
    GridPlane plane,
    float planePosition,
    bool drawMinorLines) {

    const vec3 min = grid.origin;

    const vec3 max = grid.origin +
        vec3(grid.dimensions) * grid.cellSize;

    glUseProgram(0);
    glDisable(GL_TEXTURE_2D);

    glEnable(GL_BLEND);

    glBlendFunc(
        GL_SRC_ALPHA,
        GL_ONE_MINUS_SRC_ALPHA
    );

    glLineWidth(1.0f);
    glBegin(GL_LINES);

    switch (plane) {
    // --------------------------------
    // XY — constant Z
    // --------------------------------
    case GridPlane::PLANE_XY:
        for (int x = 0; x <= grid.dimensions.x; x++) {

            const bool major =
                x % grid.majorEvery == 0;

            if (!major && !drawMinorLines)
                continue;

            const float px = min.x +
                x * grid.cellSize.x;

            if (major)
                glColor4f(1.0f, 1.0f, 1.0f, 0.32f);

            else
                glColor4f(1.0f, 1.0f, 1.0f, 0.10f);

            glVertex3f(px, min.y, planePosition);
            glVertex3f(px, max.y, planePosition);
        }

        for (int y = 0; y <= grid.dimensions.y; y++) {

            const bool major =
                y % grid.majorEvery == 0;

            if (!major && !drawMinorLines)
                continue;

            const float py = min.y +
                y * grid.cellSize.y;

            if (major)
                glColor4f(1.0f, 1.0f, 1.0f, 0.32f);

            else
                glColor4f(1.0f, 1.0f, 1.0f, 0.10f);

            glVertex3f(min.x, py, planePosition);
            glVertex3f(max.x, py, planePosition);
        }

        break;

    // --------------------------------
    // XZ — constant Y
    // --------------------------------
    case GridPlane::PLANE_XZ:
        for (int x = 0; x <= grid.dimensions.x; x++) {

            const bool major =
                x % grid.majorEvery == 0;

            if (!major && !drawMinorLines)
                continue;

            const float px = min.x +
                x * grid.cellSize.x;

            if (major)
                glColor4f(1.0f, 1.0f, 1.0f, 0.32f);

            else
                glColor4f(1.0f, 1.0f, 1.0f, 0.10f);

            glVertex3f(px, planePosition, min.z);
            glVertex3f(px, planePosition, max.z);
        }

        for (int z = 0; z <= grid.dimensions.z; z++) {

            const bool major =
                z % grid.majorEvery == 0;

            if (!major && !drawMinorLines)
                continue;

            const float pz = min.z +
                z * grid.cellSize.z;

            if (major)
                glColor4f(1.0f, 1.0f, 1.0f, 0.32f);

            else
                glColor4f(1.0f, 1.0f, 1.0f, 0.10f);

            glVertex3f(min.x, planePosition, pz);
            glVertex3f(max.x, planePosition, pz);
        }

        break;

    // --------------------------------
    // YZ — constant X
    // --------------------------------
    case GridPlane::PLANE_YZ:
        for (int y = 0; y <= grid.dimensions.y; y++) {

            const bool major =
                y % grid.majorEvery == 0;

            if (!major && !drawMinorLines)
                continue;

            const float py = min.y +
                y * grid.cellSize.y;

            if (major)
                glColor4f(1.0f, 1.0f, 1.0f, 0.32f);

            else
                glColor4f(1.0f, 1.0f, 1.0f, 0.10f);

            glVertex3f(planePosition, py, min.z);
            glVertex3f(planePosition, py, max.z);
        }

        for (int z = 0; z <= grid.dimensions.z; z++) {

            const bool major =
                z % grid.majorEvery == 0;

            if (!major && !drawMinorLines)
                continue;

            const float pz = min.z +
                z * grid.cellSize.z;

            if (major)
                glColor4f(1.0f, 1.0f, 1.0f, 0.32f);

            else
                glColor4f(1.0f, 1.0f, 1.0f, 0.10f);

            glVertex3f(planePosition, min.y, pz);
            glVertex3f(planePosition, max.y, pz);
        }

        break;

    }

    glEnd();

    glLineWidth(1.0f);
    glDisable(GL_BLEND);
}

void EuclidRenderer::drawUniformGrid(
    const UniformGrid& grid,
    const GridDisplay& display) {

    if (display.boundary)
        drawGridBoundary(grid);

    if (display.axes)
        drawGridAxes(grid);

    if (display.majorGrid) {

        const vec3 min = grid.origin;

        const vec3 max = grid.origin +
            vec3(grid.dimensions) * grid.cellSize;

        const vec3 center = 0.5f * (min + max);

        drawGridPlane(
            grid,
            GridPlane::PLANE_XY,
            center.z,
            display.minorGrid
        );

        drawGridPlane(
            grid,
            GridPlane::PLANE_XZ,
            center.y,
            display.minorGrid
        );

        drawGridPlane(
            grid,
            GridPlane::PLANE_YZ,
            center.x,
            display.minorGrid
        );
    }
}

void EuclidRenderer::drawWireCube(
    const vec3& center,
    const vec3& halfExtent) {

    const vec3 min =
        center - halfExtent;

    const vec3 max =
        center + halfExtent;

    glUseProgram(0);
    glEnable(GL_BLEND);

    glBlendFunc(
        GL_SRC_ALPHA,
        GL_ONE_MINUS_SRC_ALPHA
    );

    glLineWidth(2.0f);
    glColor4f(0.85f, 0.95f, 1.0f, 0.90f);

    glBegin(GL_LINES);

    // Bottom
    glVertex3f(min.x, min.y, min.z);
    glVertex3f(max.x, min.y, min.z);

    glVertex3f(max.x, min.y, min.z);
    glVertex3f(max.x, min.y, max.z);

    glVertex3f(max.x, min.y, max.z);
    glVertex3f(min.x, min.y, max.z);

    glVertex3f(min.x, min.y, max.z);
    glVertex3f(min.x, min.y, min.z);

    // Top
    glVertex3f(min.x, max.y, min.z);
    glVertex3f(max.x, max.y, min.z);

    glVertex3f(max.x, max.y, min.z);
    glVertex3f(max.x, max.y, max.z);

    glVertex3f(max.x, max.y, max.z);
    glVertex3f(min.x, max.y, max.z);

    glVertex3f(min.x, max.y, max.z);
    glVertex3f(min.x, max.y, min.z);

    // Vertical
    glVertex3f(min.x, min.y, min.z);
    glVertex3f(min.x, max.y, min.z);

    glVertex3f(max.x, min.y, min.z);
    glVertex3f(max.x, max.y, min.z);

    glVertex3f(max.x, min.y, max.z);
    glVertex3f(max.x, max.y, max.z);

    glVertex3f(min.x, min.y, max.z);
    glVertex3f(min.x, max.y, max.z);

    glEnd();
    
    glLineWidth(1.0f);
    glDisable(GL_BLEND);
}

void EuclidRenderer::_initGL() {

    // ---------------------------------------------------------
    // Particle sphere shader.
    // ---------------------------------------------------------
    m_program0 =
        _compileProgram(
            vertexShader,
            spherePixelShader,
            "radius",
            "particle sphere"
        );

    // ---------------------------------------------------------
    // Particle selection/highlight shader.
    // ---------------------------------------------------------
    m_program1 =
        _compileProgram(
            lightVertShader,
            lightFragShader,
            "radius",
            "particle highlight"
        );

    // ---------------------------------------------------------
    // OBJ triangle mesh shader.
    // ---------------------------------------------------------
    m_meshProgram =
        _compileProgram(
            modelVertexShader,
            modelFragmentShader,
            "normal",
            "OBJ mesh"
        );

    m_meshColorLocation = -1;
    m_meshLightDirLocation = -1;
    m_meshAmbientLocation = -1;
    m_meshTexcoordAttributeLocation = -1;
    m_meshSamplerLocation = -1;
    m_meshUseTextureLocation = -1;
    m_meshAlphaMaskLocation = -1;
    m_meshAlphaCutoffLocation = -1;
    m_meshEmissiveSamplerLocation = -1;
    m_meshUseEmissiveLocation = -1;
    m_meshEmissiveFactorLocation = -1;
    m_meshEmissiveIntensityLocation = -1;

    if (m_meshProgram) {

        m_meshColorLocation =
            glGetUniformLocation(
                m_meshProgram,
                "uColor"
            );

        m_meshLightDirLocation =
            glGetUniformLocation(
                m_meshProgram,
                "uLightDir"
            );

        m_meshAmbientLocation =
            glGetUniformLocation(
                m_meshProgram,
                "uAmbient"
            );

        m_meshTexcoordAttributeLocation =
            glGetAttribLocation(m_meshProgram, "texcoord");
        m_meshSamplerLocation =
            glGetUniformLocation(m_meshProgram, "uBaseColorTexture");
        m_meshUseTextureLocation =
            glGetUniformLocation(m_meshProgram, "uUseBaseColorTexture");
        m_meshAlphaMaskLocation =
            glGetUniformLocation(m_meshProgram, "uAlphaMask");
        m_meshAlphaCutoffLocation =
            glGetUniformLocation(m_meshProgram, "uAlphaCutoff");
        m_meshEmissiveSamplerLocation =
            glGetUniformLocation(m_meshProgram, "uEmissiveTexture");
        m_meshUseEmissiveLocation =
            glGetUniformLocation(m_meshProgram, "uUseEmissiveTexture");
        m_meshEmissiveFactorLocation =
            glGetUniformLocation(m_meshProgram, "uEmissiveFactor");
        m_meshEmissiveIntensityLocation =
            glGetUniformLocation(m_meshProgram, "uEmissiveIntensity");

        if (m_meshColorLocation < 0) {
            printf(
                "[EuclidRenderer] WARNING: OBJ mesh shader "
                "uniform uColor was not found.\n"
            );
        }

        if (m_meshLightDirLocation < 0) {
            printf(
                "[EuclidRenderer] WARNING: OBJ mesh shader "
                "uniform uLightDir was not found.\n"
            );
        }

        if (m_meshAmbientLocation < 0) {
            printf(
                "[EuclidRenderer] WARNING: OBJ mesh shader "
                "uniform uAmbient was not found.\n"
            );
        }

        // Establish safe defaults. These values remain stored
        // in the program until the future mesh draw path changes them.
        glUseProgram(m_meshProgram);

        if (m_meshColorLocation >= 0) {
            glUniform4f(
                m_meshColorLocation,
                1.0f,
                0.0f,
                0.0f,
                1.0f
            );
        }

        if (m_meshLightDirLocation >= 0) {
            glUniform3f(
                m_meshLightDirLocation,
                0.577f,
                0.577f,
                0.577f
            );
        }

        if (m_meshAmbientLocation >= 0) {
            glUniform1f(
                m_meshAmbientLocation,
                0.15f
            );
        }
        if (m_meshSamplerLocation >= 0) glUniform1i(m_meshSamplerLocation, 0);
        if (m_meshUseTextureLocation >= 0) glUniform1i(m_meshUseTextureLocation, 0);
        if (m_meshAlphaMaskLocation >= 0) glUniform1i(m_meshAlphaMaskLocation, 0);
        if (m_meshAlphaCutoffLocation >= 0) glUniform1f(m_meshAlphaCutoffLocation, 0.5f);

        glUseProgram(0);

        printf(
            "[EuclidRenderer] OBJ mesh shader uniforms: "
            "color=%d lightDir=%d ambient=%d texcoord=%d sampler=%d\n",
            m_meshColorLocation,
            m_meshLightDirLocation,
            m_meshAmbientLocation,
            m_meshTexcoordAttributeLocation,
            m_meshSamplerLocation
        );
    }

    glClampColorARB(
        GL_CLAMP_VERTEX_COLOR_ARB,
        GL_FALSE
    );

    glClampColorARB(
        GL_CLAMP_FRAGMENT_COLOR_ARB,
        GL_FALSE
    );
}

void EuclidRenderer::_initialize() {
    assert(!m_bInitialized);

    if (!m_radVBO) {
        glGenBuffers(1, &m_radVBO);
    }

    if (!m_particlePosVBO) {
        glGenBuffers(1, &m_particlePosVBO);
    }

    if (!m_particleRadVBO) {
        glGenBuffers(1, &m_particleRadVBO);
    }

    if (!m_particleColorVBO) {
        glGenBuffers(1, &m_particleColorVBO);
    }

    m_bInitialized = true;
}

void EuclidRenderer::_drawPoints(bool useColorBuffer) {
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, m_vbo);

    glVertexPointer(4, GL_FLOAT, 0, 0);
    glEnableClientState(GL_VERTEX_ARRAY);

    glBindBufferARB(GL_ARRAY_BUFFER_ARB, m_radVBO);

    glEnableVertexAttribArrayARB(1);
    glVertexAttribPointerARB(1, 1, GL_FLOAT, GL_FALSE, 0, 0);

    const bool useColors = useColorBuffer && (m_colorVBO != 0);

    if (useColors) {
        glBindBufferARB(GL_ARRAY_BUFFER_ARB, m_colorVBO);

        glColorPointer(4, GL_FLOAT, 0, 0);
        glEnableClientState(GL_COLOR_ARRAY);
    }

    glDrawArrays(GL_POINTS, 0, m_numParticles);

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableVertexAttribArrayARB(1);

    if (useColors) {
        glDisableClientState(GL_COLOR_ARRAY);
    }

    glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
}

// =============================================================================
// SHADER PROGRAM COMPILATION
// =============================================================================
GLuint EuclidRenderer::_compileProgram(
    const char* vsource,
    const char* fsource,
    const char* secondaryAttributeName,
    const char* programLabel) {

    const char* safeLabel =
        (programLabel && programLabel[0] != '\0')
        ? programLabel
        : "unnamed program";

    // ---------------------------------------------------------
    // Validate shader source.
    // ---------------------------------------------------------
    if (!vsource || !fsource) {

        printf(
            "[EuclidRenderer] Cannot compile %s: "
            "shader source is null.\n",
            safeLabel
        );

        return 0;
    }

    // ---------------------------------------------------------
    // Create shader objects.
    // ---------------------------------------------------------
    GLuint vertexShaderObject = glCreateShader(GL_VERTEX_SHADER);
    GLuint fragmentShaderObject = glCreateShader(GL_FRAGMENT_SHADER);

    if (!vertexShaderObject || !fragmentShaderObject) {

        printf(
            "[EuclidRenderer] Cannot compile %s: "
            "glCreateShader failed.\n",
            safeLabel
        );

        if (vertexShaderObject) {

            glDeleteShader(vertexShaderObject);
        }

        if (fragmentShaderObject) {

            glDeleteShader(fragmentShaderObject);
        }

        return 0;
    }


    // ---------------------------------------------------------
    // Compile vertex shader.
    // ---------------------------------------------------------
    glShaderSource(
        vertexShaderObject,
        1,
        &vsource,
        nullptr
    );

    glCompileShader(vertexShaderObject);
    GLint vertexCompiled = GL_FALSE;

    glGetShaderiv(
        vertexShaderObject,
        GL_COMPILE_STATUS,
        &vertexCompiled
    );

    if (vertexCompiled != GL_TRUE) {

        GLint logLength = 0;

        glGetShaderiv(
            vertexShaderObject,
            GL_INFO_LOG_LENGTH,
            &logLength
        );


        vector<char> log((std::max)(1, logLength));

        glGetShaderInfoLog(
            vertexShaderObject,
            static_cast<GLsizei>(log.size()),
            nullptr,
            log.data()
        );

        printf(
            "[EuclidRenderer] Failed to compile "
            "%s vertex shader:\n%s\n",
            safeLabel,
            log.data()
        );

        glDeleteShader(vertexShaderObject);
        glDeleteShader(fragmentShaderObject);

        return 0;
    }

    // ---------------------------------------------------------
    // Compile fragment shader.
    // ---------------------------------------------------------
    glShaderSource(
        fragmentShaderObject,
        1,
        &fsource,
        nullptr
    );

    glCompileShader(fragmentShaderObject);

    GLint fragmentCompiled = GL_FALSE;

    glGetShaderiv(
        fragmentShaderObject,
        GL_COMPILE_STATUS,
        &fragmentCompiled
    );

    if (fragmentCompiled != GL_TRUE) {

        GLint logLength = 0;

        glGetShaderiv(
            fragmentShaderObject,
            GL_INFO_LOG_LENGTH,
            &logLength
        );

        vector<char> log((std::max)(1, logLength));

        glGetShaderInfoLog(
            fragmentShaderObject,
            static_cast<GLsizei>(log.size()),
            nullptr,
            log.data()
        );

        printf(
            "[EuclidRenderer] Failed to compile "
            "%s fragment shader:\n%s\n",
            safeLabel,
            log.data()
        );

        glDeleteShader(vertexShaderObject);
        glDeleteShader(fragmentShaderObject);

        return 0;
    }

    // ---------------------------------------------------------
    // Create and link program.
    // ---------------------------------------------------------
    GLuint program = glCreateProgram();

    if (!program) {

        printf(
            "[EuclidRenderer] Cannot link %s: "
            "glCreateProgram failed.\n",
            safeLabel
        );

        glDeleteShader(vertexShaderObject);
        glDeleteShader(fragmentShaderObject);

        return 0;
    }

    glAttachShader(program, vertexShaderObject);
    glAttachShader(program, fragmentShaderObject);    

    // ---------------------------------------------------------
    // Preserve Ver004 attribute layout.
    //
    // Attribute 0:
    //     vertex position
    //
    // Attribute 1:
    //     radius for particle programs
    //     normal for OBJ mesh program
    // ---------------------------------------------------------
    glBindAttribLocation(program, 0, "position");

    if (secondaryAttributeName && secondaryAttributeName[0] != '\0') {

        glBindAttribLocation(
            program,
            1,
            secondaryAttributeName
        );
    }

    glLinkProgram(program);

    // ---------------------------------------------------------
    // Validate program link.
    // ---------------------------------------------------------
    GLint linkSuccess = GL_FALSE;

    glGetProgramiv(
        program,
        GL_LINK_STATUS,
        &linkSuccess
    );

    if (linkSuccess != GL_TRUE) {

        GLint logLength = 0;

        glGetProgramiv(
            program,
            GL_INFO_LOG_LENGTH,
            &logLength
        );

        vector<char> log((std::max)(1, logLength));

        glGetProgramInfoLog(
            program,
            static_cast<GLsizei>(log.size()),
            nullptr,
            log.data()
        );

        printf(
            "[EuclidRenderer] Failed to link %s:\n%s\n",
            safeLabel,
            log.data()
        );

        glDetachShader(program, vertexShaderObject);
        glDetachShader(program, fragmentShaderObject);

        glDeleteShader(vertexShaderObject);
        glDeleteShader(fragmentShaderObject);

        glDeleteProgram(program);

        return 0;
    }

    // ---------------------------------------------------------
    // Shader objects are no longer needed after linking.
    // ---------------------------------------------------------
    glDetachShader(program, vertexShaderObject);
    glDetachShader(program, fragmentShaderObject);

    glDeleteShader(vertexShaderObject);
    glDeleteShader(fragmentShaderObject);

    printf(
        "[EuclidRenderer] Shader program ready: %s "
        "(program=%u, attribute1=%s)\n",
        safeLabel,
        static_cast<unsigned int>(program),
        secondaryAttributeName
        ? secondaryAttributeName
        : "none"
    );

    return program;
}