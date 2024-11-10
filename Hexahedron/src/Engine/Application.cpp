//Hex
#include "Engine/Application.h"
#include "Engine/Logger.h"
#include "Renderer/Renderer.h"
#include "Renderer/Camera.h"

//Lib
#include <GLFW/glfw3.h>

//STL
#include <cstdio>

namespace Hex
{
	Application::Application(const ApplicationSpecification& application_spec)
	{
		Init(application_spec);
	}

	Application::~Application()
	{
		m_renderer->~Renderer();
	}

	void Application::Close()
	{
		m_running = false;
	}

	void Application::Init(const ApplicationSpecification& application_spec)
	{
		m_renderer = new Renderer(application_spec);
		m_specification = application_spec;
		m_running = true;
		Run();
	}

	void Application::Run()
	{
		float delta_time = 0.0f;
		float last_frame = 0.0f;

		m_renderer->GetCamera()->SetAspectRatio(m_specification.width / m_specification.height);
		
		while (m_running && !glfwWindowShouldClose(m_renderer->GetWindow()))
		{
			float current_frame = glfwGetTime();
			delta_time = current_frame - last_frame;
			last_frame = current_frame;
			
			m_renderer->GetCamera()->ProcessKeyboardInput(m_renderer->GetWindow(), delta_time);

			m_renderer->Tick();

			glfwPollEvents();
		}
	}
}