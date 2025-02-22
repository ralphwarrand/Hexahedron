#include "Renderer/Primitives/CubeBatch.h"
#include <iostream>

namespace Hex
{
    CubeBatch::CubeBatch()
    {
        m_cube_buffers_initialized = false;
        m_cull_back_face = true;
        m_shaded = true;
    }

    CubeBatch::~CubeBatch()
    {
        glDeleteVertexArrays(1, &m_vao);
        glDeleteBuffers(1, &m_vbo);
        glDeleteBuffers(1, &m_ebo);
    }

    void CubeBatch::AddCube(const glm::vec3& position, const float& size, const glm::vec3& color)
    {
        const float halfSize = size / 2.0f;

        // Define the vertices for a unit cube centered at the origin
        const std::vector<glm::vec3> cubeVertices = {
            { -halfSize, -halfSize,  halfSize }, // 0
            {  halfSize, -halfSize,  halfSize }, // 1
            {  halfSize,  halfSize,  halfSize }, // 2
            { -halfSize,  halfSize,  halfSize }, // 3
            { -halfSize, -halfSize, -halfSize }, // 4
            {  halfSize, -halfSize, -halfSize }, // 5
            {  halfSize,  halfSize, -halfSize }, // 6
            { -halfSize,  halfSize, -halfSize }, // 7
        };

        // Define the indices for the cube
        const std::vector<unsigned int> cubeIndices = {
            0, 1, 2, 2, 3, 0, // Front face
            4, 7, 6, 6, 5, 4, // Back face
            4, 0, 3, 3, 7, 4, // Left face
            1, 5, 6, 6, 2, 1, // Right face
            3, 2, 6, 6, 7, 3, // Top face
            4, 5, 1, 1, 0, 4, // Bottom face
        };

        // Define face normals (one per face)
        const std::vector<glm::vec3> faceNormals = {
            {  0.0f,  0.0f,  1.0f }, // Front face
            {  0.0f,  0.0f, -1.0f }, // Back face
            { -1.0f,  0.0f,  0.0f }, // Left face
            {  1.0f,  0.0f,  0.0f }, // Right face
            {  0.0f,  1.0f,  0.0f }, // Top face
            {  0.0f, -1.0f,  0.0f }, // Bottom face
        };

        // Traack current vertex offset
        const unsigned int vertex_offset = static_cast<unsigned int>(m_vertices.size());

        // Add vertex data per face
        for (size_t face = 0; face < faceNormals.size(); ++face) {
            const glm::vec3& normal = faceNormals[face];

            // Each face has 4 vertices
            for (size_t i = 0; i < 6; ++i) {
                const unsigned int index = cubeIndices[face * 6 + i];
                glm::vec3 localPosition = cubeVertices[index];
                const glm::vec3 worldPosition = localPosition + position;

                m_vertices.push_back({
                    worldPosition,
                    color,
                    normal,                  // Use the face normal here
                    glm::vec3(1.0f, 0.0f, 0.0f) // Placeholder tangent
                });
            }
        }

        // Add index data with offset
        for (int i = 0; i < cubeIndices.size(); i += 6) {
            m_indices.push_back(vertex_offset + i + 0);
            m_indices.push_back(vertex_offset + i + 1);
            m_indices.push_back(vertex_offset + i + 2);
            m_indices.push_back(vertex_offset + i + 3);
            m_indices.push_back(vertex_offset + i + 4);
            m_indices.push_back(vertex_offset + i + 5);
        }
    }

    void CubeBatch::InitBuffers()
    {
        Primitive::InitBuffers();

        if (m_cube_buffers_initialized) return;

        glBindVertexArray(m_vao);

        glGenBuffers(1, &m_ebo);
        if (m_ebo == 0) {
            std::cerr << "Error: Failed to generate Element Buffer Object (EBO)" << std::endl;
        }

        // Initialize VBO for vertex data
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_vertices.size() * sizeof(Vertex)), m_vertices.data(), GL_STATIC_DRAW);

        // Position attribute (location = 0)
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
        glEnableVertexAttribArray(0);

        // Color attribute (location = 1)
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));
        glEnableVertexAttribArray(1);

        // Normal attribute (location = 2)
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
        glEnableVertexAttribArray(2);

        // Tangent attribute (location = 3)
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, tangent));
        glEnableVertexAttribArray(3);

        if (m_indices.empty()) {
            std::cerr << "Error: No indices available to initialize EBO" << std::endl;
            return;
        }

        // Initialize EBO for indices
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_indices.size() * sizeof(unsigned int)), m_indices.data(), GL_STATIC_DRAW);

        if (const GLenum error = glGetError(); error != GL_NO_ERROR) {
            std::cerr << "OpenGL Error: " << error << " during EBO initialization" << std::endl;
        }

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        m_cube_buffers_initialized = true;
    }

    void CubeBatch::Draw()
    {
        Primitive::Draw();

        try {
            if (!m_cube_buffers_initialized) {
                InitBuffers();
                m_cube_buffers_initialized = true;
            }

            if (m_vao == 0) {
                throw std::runtime_error("Invalid VAO");
            }

            if (m_ebo == 0) {
                throw std::runtime_error("Invalid EBO");
            }

            if (m_indices.empty()) {
                throw std::runtime_error("Indices are empty");
            }

            glBindVertexArray(m_vao);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_indices.size()), GL_UNSIGNED_INT, nullptr);
            glBindVertexArray(0);
        } catch (const std::exception& e) {
            std::cerr << "Exception caught in CubeBatch::Draw: " << e.what() << std::endl;
        }
    }
}