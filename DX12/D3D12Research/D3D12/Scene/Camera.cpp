#include "stdafx.h"
#include "Camera.h"
#include "Core/input.h"
#include "External/ImGuizmo/ImGuizmo.h"

void Camera::SetPosition(const Vector3& position)
{
    m_Position = position;
    OnDirty();
}

void Camera::SetRotation(const Quaternion& rotation)
{
    m_Rotation = rotation;
    OnDirty();
}

void Camera::SetFoV(float fov)
{
    m_FoV = fov;
    OnDirty();
}

void Camera::SetAspectRatio(float aspectRatio)
{
    m_AspectRatio = aspectRatio;
    OnDirty();
}

void Camera::SetClippingPlanes(float nearPlane, float farPlane)
{
    m_NearPlane = nearPlane;
    m_FarPlane = farPlane;
    OnDirty();
}

void Camera::SetOrthographic(bool orthographic, float size)
{
    m_Perspective = !orthographic;
    if (orthographic)
    {
        m_OrthographicSize = size;
    }
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

Ray Camera::GetMouseRay(uint32_t windowWidth, uint32_t windowHeight) const
{
    Ray ray;
    Vector2 mousePos = Input::Instance().GetMousePosition();
    Vector2 ndc;
    float hw = (float)windowWidth * 0.5f;
    float hh = (float)windowHeight * 0.5f;
    ndc.x = (mousePos.x - hw) / hw;
    ndc.y = (hh - mousePos.y) / hh;
    
    Vector3 nearPoint, farPoint;
    Matrix viewProjInverse;
    m_ViewProjection.Invert(viewProjInverse);
    nearPoint = Vector3::Transform(Vector3(ndc.x, ndc.y, 0), viewProjInverse);
    farPoint = Vector3::Transform(Vector3(ndc.x, ndc.y, 1), viewProjInverse);
    ray.position = Vector3(nearPoint.x, nearPoint.y, nearPoint.z);

    ray.direction = farPoint - nearPoint;
    ray.direction.Normalize();
    return ray;
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

    if (m_Perspective)
    {
        m_Projection = Math::CreatePerspectiveMatrix(m_FoV, m_AspectRatio, m_NearPlane, m_FarPlane);
    }
    else
    {
        m_Projection = Math::CreateOrthographicMatrix(m_OrthographicSize * m_AspectRatio, m_OrthographicSize, m_NearPlane, m_FarPlane);
    }

#if 1
	constexpr Math::HaltonSequence<8, 2> x;
    constexpr Math::HaltonSequence<8, 3> y;

	m_Jitter.x = x[m_JitterIndex];
	m_Jitter.y = y[m_JitterIndex];
	m_Projection.m[2][0] += (m_Jitter.x * 2 - 1) / 1920;
	m_Projection.m[2][1] += (m_Jitter.y * 2 - 1) / 1080;
#endif

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

void Camera::Update()
{
    m_PreviousViewProjection = GetViewProjection();
    m_PrevJitter = m_Jitter;
    ++m_JitterIndex;
}

void FreeCamera::Update()
{
    Camera::Update();

    // camera movement
    Vector3 movement;
	if (Input::Instance().IsMouseDown(VK_LBUTTON))
	{
        if (ImGui::IsAnyItemActive() == false && !ImGuizmo::IsUsing())
        {
            Vector2 mouseDelta = Input::Instance().GetMouseDelta();
            Quaternion yr = Quaternion::CreateFromYawPitchRoll(0, mouseDelta.y * Time::DeltaTime() * 0.1f, 0);
            Quaternion pr = Quaternion::CreateFromYawPitchRoll(mouseDelta.x * Time::DeltaTime() * 0.1f, 0, 0);
            // yaw first, then pitch
            m_Rotation = yr * m_Rotation * pr;
        }

        movement.x -= (int)Input::Instance().IsKeyDown('A');
        movement.x += (int)Input::Instance().IsKeyDown('D');
        movement.z -= (int)Input::Instance().IsKeyDown('S');
        movement.z += (int)Input::Instance().IsKeyDown('W');
        movement.y -= (int)Input::Instance().IsKeyDown('Q');
        movement.y += (int)Input::Instance().IsKeyDown('E');
        movement = Vector3::Transform(movement, m_Rotation);
	}

    m_Velocity = Vector3::SmoothStep(m_Velocity, movement, 0.1f);
	m_Position += m_Velocity * Time::DeltaTime() * 40.0f;

    // update camera matrices
    OnDirty();
}
