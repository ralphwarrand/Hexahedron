﻿#include "Renderer/Camera.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

namespace Hex
{
	Camera::Camera(glm::vec3 position, float yaw, float pitch)
	{
		m_position = position;
		m_yaw = yaw;
		m_pitch = pitch;
		m_aspect_ratio = 16.0f / 9.0f;
		m_projection_matrix = glm::mat4(1.0f);
		UpdateProjectionMatrix();
	
		m_forward = glm::vec3(0.f, 0.f, -1.f);
		m_right = glm::vec3(1.f, 0.f, 0.f);
		m_up = glm::vec3(0.f, 1.f, 0.f);
		m_zoom = 60.f;
	}

	Camera::~Camera()
	= default;

	glm::mat4 Camera::GetViewMatrix() const
	{
		return glm::lookAt(m_position, m_position + m_forward, m_up);
	}

	glm::mat4 Camera::GetProjectionMatrix() const
	{
		return m_projection_matrix;
	}

	void Camera::ProcessKeyboardInput(GLFWwindow* window, const float& delta_time)
	{
		float velocity;
		if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
		{
			velocity = m_movement_speed * delta_time * 5.f;
		}
		else
		{
			velocity = m_movement_speed * delta_time;
		}
		
		if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
			m_position += m_forward * velocity;
		if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
			m_position -= m_forward * velocity;
		if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
			m_position -= m_right * velocity;
		if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
			m_position += m_right * velocity;
		if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
			m_position += m_up * velocity;
		if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
			m_position += -1.f * m_up * velocity;
		if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
			glfwSetWindowShouldClose(window, GLFW_TRUE);

	}

	void Camera::ProcessMouseInput(float x_offset, float y_offset, const bool constrain_pitch)
	{
		x_offset *= m_mouse_sensitivity;
		y_offset *= m_mouse_sensitivity;

		m_yaw += x_offset;
		m_pitch += y_offset;

		if (constrain_pitch) {
			if (m_pitch > 89.0f)
				m_pitch = 89.0f;
			if (m_pitch < -89.0f)
				m_pitch = -89.0f;
		}

		UpdateCameraVectors();
	}

	void Camera::ProcessMouseScroll(float yOffset)
	{
		m_zoom -= yOffset;
		if (m_zoom < 1.0f)
			m_zoom = 1.0f;
		if (m_zoom > 90.f)
			m_zoom = 90.f;
		UpdateProjectionMatrix();
	}

	void Camera::UpdateProjectionMatrix()
	{
		m_projection_matrix = glm::perspective(glm::radians(m_zoom), m_aspect_ratio, 0.1f, 1000.0f);
	}

	void Camera::UpdateCameraVectors()
	{
		glm::vec3 forward;
		forward.x = cos(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
		forward.y = sin(glm::radians(m_pitch));
		forward.z = sin(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
		m_forward = glm::normalize(forward);

		m_right = glm::normalize(glm::cross(m_forward, {0.f, 1.f, 0.f}));
		m_up = glm::normalize(glm::cross(m_right, m_forward));
	}

	void Camera::SetAspectRatio(float aspect_ratio)
	{
		m_aspect_ratio = aspect_ratio;
		UpdateProjectionMatrix();
	}
}