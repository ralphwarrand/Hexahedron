#include "pch.h"

//Hex
#include "Renderer/Renderer.h"

namespace Hex
{
	// Custom deleter function for GLFWwindow
	static void GLFWwindowDeleter(GLFWwindow* window) {
		if (window) {
			glfwDestroyWindow(window);
		}
	}

	Renderer::Renderer(entt::registry& registry, const AppSpecification& application_spec, const std::shared_ptr<Console>& console): m_window(nullptr, GLFWwindowDeleter)
		, m_registry(registry), m_console(console)  {


		Init(application_spec);
	}

	Renderer::~Renderer()
	{

	}

	void Renderer::Init(const AppSpecification& app_spec)
	{
		InitOpenGLContext(app_spec);
		LogRendererInfo();

		// Create the UBO for RenderData (binding point 0)
		glGenBuffers(1, &m_uboRenderData);
		glBindBuffer(GL_UNIFORM_BUFFER, m_uboRenderData);
		glBufferData(GL_UNIFORM_BUFFER, sizeof(RenderData), nullptr, GL_DYNAMIC_DRAW);
		// Make it available as binding point 0
		glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_uboRenderData);
		glBindBuffer(GL_UNIFORM_BUFFER, 0);

		InitShadowMap();

		m_camera.reset(new Camera({-10.f, 10.f, 10.f}, -45.0f, -20.f));
		InitFrameBuffer(app_spec.width, app_spec.height);


		m_screen_quad.reset(new ScreenQuad());


		SetupCallBacks();
		
		glfwSetInputMode(m_window.get(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);



	}

	void Renderer::Tick(const float& delta_time)
	{
		// Process input
		m_camera->ProcessKeyboardInput(m_window.get(), delta_time);
		m_camera->Tick(delta_time);

		BindWindowBuffer();
		StartImGuiFrame();

		UpdateRenderData();
		if(!m_wireframe_mode) RenderShadowMap();		// First pass: Generate shadow map

		BindFrameBuffer();								// Switch to primary frame buffer
		if(!m_wireframe_mode) RenderFullScreenQuad();	// Second pass: Render sky background
		//RenderScene();									// Third pass: Render scene with shadows
		RenderSceneBatched();

		glBindFramebuffer(GL_FRAMEBUFFER, 0); // Unbind frame buffer

		// Render ImGui interface
		ShowDebugUI(delta_time);
		m_console->Render();

		EndImGuiFrame(delta_time);

		glfwSwapBuffers(m_window.get());
	}


	void Renderer::InitOpenGLContext(const AppSpecification& app_spec)
	{
		if (!glfwInit()) {
			Log(LogLevel::Fatal, "GLFW failed to initialise");
			exit(EXIT_FAILURE);
		}
		else
		{
			Log(LogLevel::Info, "GLFW initialised");
		}

		//using OpenGL 4.6
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

		//TODO: Remove from release build?
		glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);

		if(app_spec.fullscreen)
		{
			m_window.reset(glfwCreateWindow(app_spec.width, app_spec.height, app_spec.name.c_str(), glfwGetPrimaryMonitor(), nullptr));
		}
		else
		{
			m_window.reset(glfwCreateWindow(app_spec.width, app_spec.height, app_spec.name.c_str(), nullptr, nullptr));
		}
		
		if (!m_window)
		{
			Log(LogLevel::Fatal, "GLFW failed to create window");
			glfwTerminate();
			exit(EXIT_FAILURE);
		}
		Log(LogLevel::Info, "GLFW window created");
		
		//set callbacks
		glfwSetErrorCallback([](const int error, const char* description)
		{
			const std::string error_string = std::to_string(error) + ":" + description;
			Log(LogLevel::Error, error_string);
		});


		glfwMakeContextCurrent(m_window.get());

		if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
		{
			std::cout << "Failed to initialize GLAD" << std::endl;
		}

		glViewport(0, 0, app_spec.width, app_spec.height);

		glEnable(GL_DEBUG_OUTPUT);

		int flags;
		glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
		if (flags & GL_CONTEXT_FLAG_DEBUG_BIT) {
			Log(LogLevel::Info, "OpenGL debug context enabled");
		}

		if(app_spec.vsync)
		{
			glfwSwapInterval(1); // Enable VSync
		}
		else
		{
			glfwSwapInterval(0); // Disable VSync
		}

		Texture::InitDefaults();

	}

	void Renderer::SetupCallBacks()
	{
		glfwSetWindowUserPointer(m_window.get(), this);


		glfwSetCursorPosCallback(m_window.get(), [](GLFWwindow* window, const double x_delta, const double y_delta)
		{
			const auto* self = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
					
			static double last_x = x_delta, last_y = y_delta;
			static bool first_mouse = true;
					
			if (first_mouse) {
				last_x = x_delta;
				last_y = y_delta;
				first_mouse = false;
			}
		
			const double x_offset = x_delta - last_x;
			const double y_offset = last_y - y_delta; // Reversed since y-coordinates go from bottom to top
					
			last_x = x_delta;
			last_y = y_delta;
					
			self->m_camera->ProcessMouseInput(x_offset, y_offset);
		});

		glfwSetScrollCallback(m_window.get(), [](GLFWwindow* window, double xoffset, const double yoffset)
		{
			const auto* self = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
			self->m_camera->ProcessMouseScroll(static_cast<float>(yoffset));
		});
	}

	void Renderer::LogRendererInfo()
	{
		Log(LogLevel::Info, std::format("Running GLFW {}", glfwGetVersionString()));
		Log(LogLevel::Info, std::format("Running OpenGL {}", reinterpret_cast<const char*>(glGetString(GL_VERSION))));
		Log(LogLevel::Info, std::format("Running GLSL {}", reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION))));
		Log(LogLevel::Info, std::format("Using GPU: {}", reinterpret_cast<const char*>(glGetString(GL_RENDERER))));
		Log(LogLevel::Info, "Renderer Initialised\n");
	}

	void Renderer::CheckFrameBufferStatus()
	{
		GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		switch (status)
		{
			case GL_FRAMEBUFFER_COMPLETE:
				Log(LogLevel::Info, "Framebuffer is complete.");
			break;
			case GL_FRAMEBUFFER_UNDEFINED:
				Log(LogLevel::Error, "GL_FRAMEBUFFER_UNDEFINED: The specified framebuffer is the default read or draw framebuffer, but the default framebuffer does not exist.");
			break;
			case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
				Log(LogLevel::Error, "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT: One or more framebuffer attachment points are incomplete.");
			break;
			case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
				Log(LogLevel::Error, "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: The framebuffer does not have at least one image attached.");
			break;
			case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
				Log(LogLevel::Error, "GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER: The value of GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE is GL_NONE for one or more color attachment points.");
			break;
			case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
				Log(LogLevel::Error, "GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER: The value of GL_READ_BUFFER is not GL_NONE, and the value of GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE is GL_NONE for the color attachment point.");
			break;
			case GL_FRAMEBUFFER_UNSUPPORTED:
				Log(LogLevel::Error, "GL_FRAMEBUFFER_UNSUPPORTED: The combination of internal formats of the attached images violates an implementation-dependent set of restrictions.");
			break;
			case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
				Log(LogLevel::Error, "GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE: The number of samples for all attachments is not the same.");
			break;
			case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:
				Log(LogLevel::Error, "GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS: A framebuffer attachment is layered, and a populated attachment is not layered.");
			break;
			default:
				Log(LogLevel::Error, "Unknown framebuffer error.");
		}
	}

	void Renderer::InitShadowMap()
	{
		// Generate and configure the shadow map framebuffer
		glGenFramebuffers(1, &m_shadow_map.fbo);

		// Create the depth texture
		glGenTextures(1, &m_shadow_map.texture);
		glBindTexture(GL_TEXTURE_2D, m_shadow_map.texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F,
			m_shadow_map.shadow_width, m_shadow_map.shadow_height, 0, GL_DEPTH_COMPONENT,
			GL_FLOAT, nullptr);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		// enable GLSL sampler2DShadow-style comparisons
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

		constexpr float border_color[] = { 1.0f, 1.0f, 1.0f, 1.0f };
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
		glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border_color);

		glBindFramebuffer(GL_FRAMEBUFFER, m_shadow_map.fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_shadow_map.texture, 0);
		glDrawBuffer(GL_NONE);
		glReadBuffer(GL_NONE);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			Log(LogLevel::Error, "Shadow map framebuffer is incomplete!");

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void Renderer::InitFrameBuffer(const int& width, const int& height)
	{
		if (width <= 0 || height <= 0) {
			Log(LogLevel::Error, "Invalid framebuffer dimensions");
			return;
		}

		// Cleanup existing framebuffer
		if (m_frame_buffer.frame_buffer) glDeleteFramebuffers(1, &m_frame_buffer.frame_buffer);
		if (m_frame_buffer.texture) glDeleteTextures(1, &m_frame_buffer.texture);
		if (m_frame_buffer.depth_render_buffer) glDeleteRenderbuffers(1, &m_frame_buffer.depth_render_buffer);

		// Create framebuffer
		glGenFramebuffers(1, &m_frame_buffer.frame_buffer);
		glBindFramebuffer(GL_FRAMEBUFFER, m_frame_buffer.frame_buffer);

		// Create and attach color texture
		glGenTextures(1, &m_frame_buffer.texture);
		glBindTexture(GL_TEXTURE_2D, m_frame_buffer.texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_frame_buffer.texture, 0);

		// Create and attach depth-stencil renderbuffer
		glGenRenderbuffers(1, &m_frame_buffer.depth_render_buffer);
		glBindRenderbuffer(GL_RENDERBUFFER, m_frame_buffer.depth_render_buffer);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_frame_buffer.depth_render_buffer);

		// Check framebuffer completeness
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
			CheckFrameBufferStatus(); // Logs detailed error
		} else {
			Log(LogLevel::Info, "Framebuffer initialized successfully.");
		}

		m_camera->SetAspectRatio(static_cast<float>(m_frame_buffer.render_width)/static_cast<float>(m_frame_buffer.render_height));

		glBindFramebuffer(GL_FRAMEBUFFER, 0); // Unbind framebuffer
	}

	void Renderer::BindFrameBuffer() const
	{
		// Bind framebuffer for rendering
		glBindFramebuffer(GL_FRAMEBUFFER, m_frame_buffer.frame_buffer);
		glViewport(0, 0, m_frame_buffer.render_width, m_frame_buffer.render_height); // Match viewport size to framebuffer
		glClearColor(0.f, 0.f, 0.f, 1.0f); // Sky blue color
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glEnable(GL_DEPTH_TEST);
	}

	void Renderer::BindWindowBuffer() const
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0); // Unbind framebuffer
		int width{0}, height{0};
		glfwGetFramebufferSize(m_window.get(), &width, &height);
		glViewport(0, 0, width, height); // Match viewport size to framebuffer
		glClearColor(0.08f, 0.10f, 0.12f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

	void Renderer::RenderShadowMap()
	{
	    glBindFramebuffer(GL_FRAMEBUFFER, m_shadow_map.fbo);
	    glViewport(0, 0, m_shadow_map.shadow_width, m_shadow_map.shadow_height);
	    glClear(GL_DEPTH_BUFFER_BIT);

	    glEnable(GL_DEPTH_TEST);
	    glEnable(GL_POLYGON_OFFSET_FILL);
	    glPolygonOffset(2.0f, 4.0f);
	    glCullFace(GL_FRONT);
	    glEnable(GL_CULL_FACE);
	    glDrawBuffer(GL_NONE);

		// Compute the “scene box” we want to shadow:
		const float R = 10.0f;  // adjust to cover your scene
		glm::vec3 center = glm::vec3(0.0f);


		// Get your light’s direction (unit vector).
		glm::vec3 dir = glm::normalize(m_light_dir);

		// Place the shadow‐camera “behind” the box along -dir at distance R.
		glm::vec3 shadowCamPos = center - dir * R;

		// Build view/proj
		m_shadow_map.light_view       = glm::lookAt(shadowCamPos, center, {0,1,0});
		m_shadow_map.light_projection = glm::ortho(-R, R, R, -R, 0.1f, 2.0f*R);

	    // bind shadow shader
	    auto shadow_shader = ShaderManager::GetOrCreateShader(
	        RESOURCES_PATH "shaders/shadow.vert",
	        RESOURCES_PATH "shaders/shadow.frag"
	    );
	    shadow_shader->Bind();
	    shadow_shader->SetUniformMat4("light_view",       m_shadow_map.light_view);
	    shadow_shader->SetUniformMat4("light_projection", m_shadow_map.light_projection);

	    // --- gather all mesh+transform items ---
	    struct Item { Mesh* mesh; glm::mat4 model; };
	    std::vector<Item> items;

	    // individual MeshComponents
	    for (auto e : m_registry.view<TransformComponent, MeshComponent>()) {
	        auto &tc = m_registry.get<TransformComponent>(e);
	        auto &mc = m_registry.get<MeshComponent>(e);
	        items.push_back({ mc.mesh.get(), tc.GetMatrix() });
	    }
	    // sub‐meshes in ModelComponents
	    for (auto e : m_registry.view<TransformComponent, ModelComponent>()) {
	        auto &tc  = m_registry.get<TransformComponent>(e);
	        auto &mdc = m_registry.get<ModelComponent>(e);
	        for (auto &sub : mdc.model->GetMeshes())
	            items.push_back({ sub.get(), tc.GetMatrix() });
	    }

	    if (items.empty()) {
	        glCullFace(GL_BACK);
	        glDisable(GL_CULL_FACE);
	        glDisable(GL_POLYGON_OFFSET_FILL);
	        glBindFramebuffer(GL_FRAMEBUFFER, 0);
	        return;
	    }

	    // --- sort and draw instanced per mesh ---
	    std::sort(items.begin(), items.end(),
	              [](auto const &a, auto const &b){ return a.mesh < b.mesh; });

	    size_t idx = 0;
	    while (idx < items.size()) {
	        Mesh* mesh = items[idx].mesh;

	        // collect all models for this mesh
	        std::vector<glm::mat4> models;
	        size_t j = idx;
	        for (; j < items.size() && items[j].mesh == mesh; ++j)
	            models.push_back(items[j].model);

	        // upload per-instance buffer
	        glBindBuffer(GL_ARRAY_BUFFER, mesh->instanceVBO);
	        glBufferData(GL_ARRAY_BUFFER,
	                     models.size() * sizeof(glm::mat4),
	                     models.data(),
	                     GL_DYNAMIC_DRAW);
	        glBindBuffer(GL_ARRAY_BUFFER, 0);

	        // single instanced draw
	        glBindVertexArray(mesh->VAO);
	        glDrawElementsInstanced(
	            GL_TRIANGLES,
	            mesh->indexCount,
	            GL_UNSIGNED_INT,
	            nullptr,
	            static_cast<GLsizei>(models.size())
	        );
	        glBindVertexArray(0);

	        idx = j;
	    }

	    Shader::Unbind();
	    glCullFace(GL_BACK);
	    glDisable(GL_CULL_FACE);
	    glDisable(GL_POLYGON_OFFSET_FILL);
	    glBindFramebuffer(GL_FRAMEBUFFER, 0);

	    // restore viewport to window
	    int w, h;
	    glfwGetFramebufferSize(m_window.get(), &w, &h);
	    glViewport(0, 0, w, h);
	}

	void Renderer::RenderFullScreenQuad() const
	{
		glDisable(GL_DEPTH_TEST);

		// Use the gradient shader
		auto gradientShader = ShaderManager::GetOrCreateShader(
			RESOURCES_PATH "shaders/gradient.vert",
			RESOURCES_PATH "shaders/gradient.frag"
		);
		gradientShader->Bind();

		// upload inverse matrices
		glm::mat4 invProj = glm::inverse(m_camera->GetProjectionMatrix());
		glm::mat4 invView = glm::inverse(m_camera->GetViewMatrix());
		gradientShader->SetUniformMat4("inverseProjection", invProj);
		gradientShader->SetUniformMat4("invView", invView);

		// upload sun dir & colors
		gradientShader->SetUniformVec3("light_dir", glm::normalize(m_light_dir));
		gradientShader->SetUniformVec3("topColor",  glm::vec3(0.53f,0.81f,0.92f));
		gradientShader->SetUniformVec3("bottomColor", glm::vec3(0.87f,0.94f,1.0f));
		gradientShader->SetUniform1f("mieG", 0.8f);

		glBindVertexArray(m_screen_quad.get()->vao);
		glDrawArrays(GL_TRIANGLES, 0, 6); // Draw the quad as two triangles
		glBindVertexArray(0);

		Shader::Unbind();

		glEnable(GL_DEPTH_TEST);
	}

	void Renderer::RenderScene() const
	{
		glm::mat4 lightSpace = m_shadow_map.light_projection * m_shadow_map.light_view;

		for (auto e : m_registry.view<TransformComponent, MeshComponent>()) {
			auto &tc = m_registry.get<TransformComponent>(e);
			auto &mc = m_registry.get<MeshComponent>(e);
			auto &mat= m_registry.get<MaterialComponent>(e);

			mat.material->Apply();

			glActiveTexture(GL_TEXTURE4);
			glBindTexture(GL_TEXTURE_2D, m_shadow_map.texture);
			mat.material->shader->SetUniformMat4("model", tc.GetMatrix());
			mat.material->shader->SetUniformMat4("light_space_matrix", lightSpace);
			mat.material->shader->SetUniform1i("should_shade", 1);
			mat.material->shader->SetUniform1i("shadow_map", 4);

			mc.mesh->Draw();
		}

		for (auto e : m_registry.view<TransformComponent, ModelComponent>()) {
			auto &tc = m_registry.get<TransformComponent>(e);
			auto &mc = m_registry.get<ModelComponent>(e);
			auto &mat= m_registry.get<MaterialComponent>(e);

			mat.material->Apply();

			glActiveTexture(GL_TEXTURE4);
			glBindTexture(GL_TEXTURE_2D, m_shadow_map.texture);
			mat.material->shader->SetUniformMat4("model", tc.GetMatrix());
			mat.material->shader->SetUniformMat4("light_space_matrix", lightSpace);
			mat.material->shader->SetUniform1i("should_shade", 1);
			mat.material->shader->SetUniform1i("shadow_map", 4);

			mc.model->Draw();
		}

	}

	void Renderer::RenderSceneBatched() const {
		glm::mat4 lightSpace = m_shadow_map.light_projection * m_shadow_map.light_view;

		struct Item { Material* mat; Mesh* mesh; glm::mat4 model; };
		std::vector<Item> items;

		// Gather MeshComponents
		{
			auto view = m_registry.view<TransformComponent, MeshComponent, MaterialComponent>();
			items.reserve(std::distance(view.begin(), view.end()));
			for (auto e : view) {
				auto& tc  = m_registry.get<TransformComponent>(e);
				auto& mc  = m_registry.get<MeshComponent>(e);
				auto& mat = m_registry.get<MaterialComponent>(e);
				items.push_back({ mat.material.get(),
								  mc.mesh.get(),
								  tc.GetMatrix() });
			}
		}

		// Gather ModelComponents (each model may have multiple sub-meshes)
		{
			auto view = m_registry.view<TransformComponent, ModelComponent, MaterialComponent>();
			for (auto e : view) {
				auto& tc   = m_registry.get<TransformComponent>(e);
				auto& mdc  = m_registry.get<ModelComponent>(e);
				auto& mat  = m_registry.get<MaterialComponent>(e);
				for (auto& submesh : mdc.model->GetMeshes()) {
					items.push_back({ mat.material.get(),
									  submesh.get(),
									  tc.GetMatrix() });
				}
			}
		}

		if (items.empty()) {
			// nothing to draw
			return;
		}

		// Sort by material, then mesh
		std::sort(items.begin(), items.end(), [](auto const &a, auto const &b) {
			if (a.mat  != b.mat)  return a.mat  < b.mat;
			return   a.mesh < b.mesh;
		});


		size_t idx = 0;
		while (idx < items.size()) {
			auto mat  = items[idx].mat;
			auto mesh = items[idx].mesh;

			// collect per-instance matrices
			std::vector<glm::mat4> models;
			size_t j = idx;
			for (; j < items.size() && items[j].mat == mat && items[j].mesh == mesh; ++j)
				models.push_back(items[j].model);
			GLsizei instanceCount = GLsizei(models.size());

			// upload instance‐models
			glBindBuffer(GL_ARRAY_BUFFER, mesh->instanceVBO);
			glBufferData(GL_ARRAY_BUFFER,
						 instanceCount * sizeof(glm::mat4),
						 models.data(),
						 GL_DYNAMIC_DRAW);

			// set up material + PBR maps
			mat->Apply();

			// set per‐draw uniforms
			auto s = mat->shader.get();
			s->SetUniformMat4("light_space_matrix", lightSpace);
			s->SetUniform1i("should_shade",        1);

			// bind shadow map
			glActiveTexture(GL_TEXTURE5);
			glBindTexture(GL_TEXTURE_2D, m_shadow_map.texture);
			s->SetUniform1i("shadow_map", 5);

			// draw instanced
			glBindVertexArray(mesh->VAO);
			glDrawElementsInstanced(
				GL_TRIANGLES,
				mesh->indexCount,
				GL_UNSIGNED_INT,
				nullptr,
				instanceCount
			);
			glBindVertexArray(0);

			idx = j;
		}

		Shader::Unbind();
	}

	GLFWwindow* Renderer::GetWindow() const
	{
		return m_window.get();
	}

	Camera* Renderer::GetCamera() const
	{
		return m_camera.get();
	}

	void Renderer::UpdateRenderData()
	{
		//if(m_render_data == m_old_render_data) return;

		m_old_render_data = m_render_data;

		// Update view and projection matrices from the camera
		m_render_data.view = m_camera->GetViewMatrix();          // View matrix from the camera
		m_render_data.projection = m_camera->GetProjectionMatrix(); // Projection matrix from the camera

		// Update the camera's position
		m_render_data.view_pos = m_camera->GetPosition();

		// Padding explicitly set to 0 (required for std140 uniform alignment)
		m_render_data.padding1 = 0.0f;

		// Update the directional‐light direction and color
		m_render_data.light_dir   = glm::normalize(m_light_dir);
		m_render_data.padding2    = 0.0f;             // explicit zero for std140 padding
		m_render_data.light_color = m_light_color;
		m_render_data.padding3    = 0.0f;

		// Update wireframe mode (bool is treated as 4-byte int in std140)
		m_render_data.wireframe = m_wireframe_mode;

		m_render_data.padding4[0]  = m_render_data.padding4[1] = m_render_data.padding4[2] = 0.0f;

		glBindBuffer(GL_UNIFORM_BUFFER, m_uboRenderData);
		glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(RenderData), &m_render_data);
		glBindBuffer(GL_UNIFORM_BUFFER, 0);
	}

	void Renderer::SetLightDir(const glm::vec3 &dir)
	{
		m_light_dir = dir;
		m_render_data.light_dir = dir;
	}

	void Renderer::StartImGuiFrame()
	{
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		// Create a dock space directly over the viewport
		ImGui::DockSpaceOverViewport(ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
	}

	void Renderer::EndImGuiFrame(const float& delta_time)
	{
		// Render ImGui
		ImGui::Render();

		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		// Handle multi-viewports
		ImGuiIO& io = ImGui::GetIO();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			// Save the current OpenGL context
			GLFWwindow* backup_context = glfwGetCurrentContext();

			// Update and render all platform windows
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();

			// Restore the saved context
			glfwMakeContextCurrent(backup_context);
		}
	}

	void Renderer::ShowDebugUI(const float& delta_time)
	{
		// Start the main menu bar
		if (ImGui::BeginMainMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("Exit")) {
					glfwSetWindowShouldClose(m_window.get(), true);
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("View"))
			{
				ImGui::MenuItem("Rendering Metrics", nullptr, &m_show_metrics);
				ImGui::MenuItem("Scene Information", nullptr, &m_show_scene_info);
				ImGui::MenuItem("Lighting Tool", nullptr, &m_show_lighting_tool);
				ImGui::MenuItem("Wireframe", nullptr, &m_wireframe_mode);

				if (m_wireframe_mode)
				{
					glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); // Enable wireframe
				}
				else
				{
					glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); // Disable wireframe
				}

				ImGui::EndMenu();
			}
			ImGui::EndMainMenuBar();
		}

		// Other windows (metrics, scene info, etc.)
		if (m_show_metrics)
		{
			if (ImGui::Begin("Rendering Metrics", &m_show_metrics))
			{
				ImGui::Text("FPS: %.1f", 1.0f / delta_time);
				ImGui::Text("Frame-time: %.6f ms", delta_time * 1000.0f);
			}
			ImGui::End();
		}

		if (m_show_scene_info)
		{
			if (ImGui::Begin("Scene Information", &m_show_scene_info))
			{
				ImGui::Text("Camera Position:");
				ImGui::Text("%.2f, %.2f, %.2f",
							m_camera->GetPosition().x,
							m_camera->GetPosition().y,
							m_camera->GetPosition().z);
				ImGui::Separator();


				// Display Camera View Matrix
				ImGui::Text("Camera View:");
				glm::mat4 viewMatrix = m_camera->GetViewMatrix();
				for (int i = 0; i < 4; ++i) {
					ImGui::Text("%.2f, %.2f, %.2f, %.2f",
								viewMatrix[i][0], viewMatrix[i][1], viewMatrix[i][2], viewMatrix[i][3]);
				}
				ImGui::Separator();

				// Display Camera Projection Matrix
				ImGui::Text("Camera Proj:");
				glm::mat4 projMatrix = m_camera->GetProjectionMatrix();
				for (int i = 0; i < 4; ++i) {
					ImGui::Text("%.2f, %.2f, %.2f, %.2f",
								projMatrix[i][0], projMatrix[i][1], projMatrix[i][2], projMatrix[i][3]);
				}

				// Display Primitives Information
				if (ImGui::CollapsingHeader("Primitives Information"))
				{
				}
			}
			ImGui::End();
		}
		// Lighting Tool Window
		if (m_show_lighting_tool)
		{
			if (ImGui::Begin("Lighting Tool", &m_show_lighting_tool)) // Allow closing
			{
				constexpr int active_lights_count = 1; // TODO: update to reflect actual light count
				constexpr int selected_light_index = 1; // TODO: update to reflect actual selected light index

				// Display static lighting information
				ImGui::Text("Active Lights: %d", active_lights_count);
				ImGui::Text("Selected Light: %d", selected_light_index);
				ImGui::Text("Light Direction:");

				// Add interactive controls for editing the light position
				if (ImGui::DragFloat3("LightDirection", &m_light_dir.x, 0.1f, -1.0f, 1.0f))
				{
					SetLightDir(m_light_dir);
				}

				ImGui::Separator();

				// Shadow Mapping
				if (ImGui::CollapsingHeader("Shadow Mapping"))
				{
					static float shadow_zoom = 1.0f; // Zoom factor
					static glm::vec2 shadow_pan(0.0f, 0.0f); // Pan offsets

					ImGui::Text("Shadow Map");

					// Add controls for zoom and pan
					ImGui::SliderFloat("Zoom", &shadow_zoom, 0.1f, 5.0f, "Zoom: %.2f");
					ImGui::DragFloat2("Pan", &shadow_pan.x, 0.01f, -1.0f, 1.0f, "Pan: %.2f");

					// Calculate the UV range for zoom
					float uv_range = 0.5f / shadow_zoom;
					glm::vec2 uv_center = glm::vec2(0.5f) + shadow_pan * uv_range;

					// Clamp the UV center to avoid going out of bounds
					uv_center.x = glm::clamp(uv_center.x, uv_range, 1.0f - uv_range);
					uv_center.y = glm::clamp(uv_center.y, uv_range, 1.0f - uv_range);

					ImVec2 uv_min(uv_center.x - uv_range, uv_center.y - uv_range);
					ImVec2 uv_max(uv_center.x + uv_range, uv_center.y + uv_range);

					// Display the shadow map with the calculated UVs
					ImVec2 image_size(300, 300); // Fixed display size
					ImGui::Image((void*)(intptr_t)m_shadow_map.texture, image_size, uv_min, uv_max);
				}


			}
			ImGui::End();
		}

		// Viewport Window
		ImGui::Begin("Viewport");

		// Get the size of the viewport panel
		ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
		int newWidth = static_cast<int>(viewportPanelSize.x);
		int newHeight = static_cast<int>(viewportPanelSize.y);

		// Resize framebuffer if dimensions change
		if (newWidth > 0 && newHeight > 0 &&
			(newWidth != m_frame_buffer.render_width || newHeight != m_frame_buffer.render_height))
		{
			m_frame_buffer.render_width = newWidth;
			m_frame_buffer.render_height = newHeight;
			InitFrameBuffer(m_frame_buffer.render_width, m_frame_buffer.render_height);
		}

		// Display the framebuffer texture in ImGui
		ImGui::Image((void*)(intptr_t)m_frame_buffer.texture,
					 ImVec2(static_cast<float>(m_frame_buffer.render_width), static_cast<float>(m_frame_buffer.render_height)),
					 ImVec2(0, 1), ImVec2(1, 0));
		ImGui::End();
	}
}
