#include "stdafx.h"
#include "Camera.h"
#include "Graphics/Core/Graphics.h"
#include "Core/input.h"

Camera::Camera(Graphics* pGraphics)
    : m_pGraphics(pGraphics)
{
}

Camera::~Camera()
{
}

void Camera::SetPosition(const Vector3& position)
{
    m_Position = position;
}

void Camera::SetRotation(const Quaternion& rotation)
{
    m_Rotation = rotation;
}

FloatRect Camera::GetAbsoluteViewport() const
{
    float renderTargetWidth = (float)m_pGraphics->GetWindowWidth();
    float renderTargetHeight = (float)m_pGraphics->GetWindowHeight();
    
    FloatRect absoluteViewport;
    absoluteViewport.Left = m_Viewport.Left * renderTargetWidth;
    absoluteViewport.Top = m_Viewport.Top * renderTargetHeight;
    absoluteViewport.Right = m_Viewport.Right * renderTargetWidth;
    absoluteViewport.Bottom = m_Viewport.Bottom * renderTargetHeight;
    return absoluteViewport;
}

void Camera::SetFoV(float fov)
{
    m_FoV = fov;
    OnDirty();
}

void Camera::SetViewport(float x, float y, float width, float height)
{
    m_Viewport.Left = x;
    m_Viewport.Top = y;
    m_Viewport.Right = x + width;
    m_Viewport.Bottom = y + height;
    OnDirty();
}

void Camera::SetClippingPlanes(float nearPlane, float farPlane)
{
    m_NearPlane = nearPlane;
    m_FarPlane = farPlane;
    OnDirty();
}

void Camera::SetOrthographic(bool orthographic)
{
    m_Perspective = !orthographic;
    OnDirty();
}

void Camera::SetOrthographicSize(float size)
{
    m_Size = size;
    OnDirty();
}

void Camera::SetNewPlane(float nearPlane)
{
    m_NearPlane = nearPlane;
    OnDirty();
}

void Camera::SetFarPlane(float farPlane)
{
    m_FarPlane = farPlane;
    OnDirty();
}

const Matrix& Camera::GetView() const
{
    UpdateMatrices();
    return m_View;
}

const Matrix& Camera::GetProjection() const
{
    UpdateMatrices();
    return m_Projection;
}

const Matrix& Camera::GetViewProjection() const
{
    UpdateMatrices();
    return m_ViewProjection;
}

const Matrix& Camera::GetViewInverse() const
{
    UpdateMatrices();
    return m_ViewInverse;
}

const Matrix& Camera::GetProjectionInverse() const
{
    UpdateMatrices();
    return m_ProjectionInverse;
}

const BoundingFrustum& Camera::GetFrustum() const
{
    UpdateMatrices();
    return m_Frustum;
}

void Camera::OnDirty()
{
    m_Dirty = true;
}

void Camera::UpdateMatrices() const
{
    if (!m_Dirty)
        return;

    m_Dirty = false;

    m_ViewInverse = Matrix::CreateFromQuaternion(m_Rotation) * Matrix::CreateTranslation(m_Position);
    m_ViewInverse.Invert(m_View);

    FloatRect rect = GetAbsoluteViewport();
    if (m_Perspective)
    {
        m_Projection = Math::CreatePerspectiveMatrix(m_FoV, rect.GetWidth() / rect.GetHeight(), m_NearPlane, m_FarPlane);
    }
    else
    {
        m_Projection = Math::CreateOrthographicMatrix(rect.GetWidth(), rect.GetHeight(), m_NearPlane, m_FarPlane);
    }

    m_Projection.Invert(m_ProjectionInverse);
    m_ViewProjection = m_View * m_Projection;

    Matrix p = m_Projection;
    if (m_FarPlane < m_NearPlane)
    {
        Math::ReverseZProjection(p);
    }
    BoundingFrustum::CreateFromMatrix(m_Frustum, p);
    m_Frustum.Transform(m_Frustum, m_ViewInverse);
}

FreeCamera::FreeCamera(Graphics* pGraphics)
    : Camera(pGraphics)
{
}

void FreeCamera::Update()
{
    // camera movement
	if (Input::Instance().IsMouseDown(VK_LBUTTON) && ImGui::IsAnyItemActive() == false)
	{
		Vector2 mouseDelta = Input::Instance().GetMouseDelta();
		Quaternion yr = Quaternion::CreateFromYawPitchRoll(0, mouseDelta.y * GameTimer::DeltaTime() * 0.1f, 0);
		Quaternion pr = Quaternion::CreateFromYawPitchRoll(mouseDelta.x * GameTimer::DeltaTime() * 0.1f, 0, 0);
		// yaw first, then pitch
        m_Rotation = yr * m_Rotation * pr;
	}

	Vector3 movement;
	movement.x -= (int)Input::Instance().IsKeyDown('A');
	movement.x += (int)Input::Instance().IsKeyDown('D');
	movement.z -= (int)Input::Instance().IsKeyDown('S');
	movement.z += (int)Input::Instance().IsKeyDown('W');
	movement.y -= (int)Input::Instance().IsKeyDown('Q');
	movement.y += (int)Input::Instance().IsKeyDown('E');
	movement = Vector3::Transform(movement, m_Rotation);

    m_Velocity = Vector3::SmoothStep(m_Velocity, movement, 0.1f);
	m_Position += m_Velocity * GameTimer::DeltaTime() * 40.0f;

    // update camera matrices
    OnDirty();
}