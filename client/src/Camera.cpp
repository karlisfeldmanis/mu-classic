#include "Camera.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>

Camera::Camera(glm::vec3 position)
    : Position(position), WorldUp(0.0f, 1.0f, 0.0f), Yaw(-45.0f), Pitch(-48.5f),
      Zoom(800.0f), TargetZoom(800.0f), MovementSpeed(2000.0f),
      MouseSensitivity(0.25f) {
  updateCameraVectors();
}

void Camera::ProcessKeyboard(int direction, float deltaTime) {
  float velocity = MovementSpeed * deltaTime;

  // Direction: 0=Forward, 1=Backward, 2=Left, 3=Right (WASD)
  // Movement is relative to Yaw (horizontal plane)
  glm::vec3 front_horizontal =
      glm::normalize(glm::vec3(Front.x, 0.0f, Front.z));
  glm::vec3 right_horizontal =
      glm::normalize(glm::cross(front_horizontal, WorldUp));

  if (direction == 0)
    Position += front_horizontal * velocity;
  if (direction == 1)
    Position -= front_horizontal * velocity;
  if (direction == 2)
    Position -= right_horizontal * velocity;
  if (direction == 3)
    Position += right_horizontal * velocity;
}

void Camera::ProcessMouseRotation(float xoffset, float yoffset) {
  xoffset *= MouseSensitivity;
  yoffset *= MouseSensitivity;

  Yaw += xoffset;
  Pitch += yoffset;

  // Constrain pitch to avoid screen flip
  if (Pitch > -5.0f)
    Pitch = -5.0f;
  if (Pitch < -89.0f)
    Pitch = -89.0f;

  updateCameraVectors();
}

void Camera::ProcessMouseScroll(float yoffset) {
  constexpr float ZOOM_STEP = 40.0f;
  constexpr float MIN_ZOOM = 400.0f;
  constexpr float MAX_ZOOM = 1200.0f;
  TargetZoom -= yoffset * ZOOM_STEP;
  if (TargetZoom < MIN_ZOOM) TargetZoom = MIN_ZOOM;
  if (TargetZoom > MAX_ZOOM) TargetZoom = MAX_ZOOM;
}

void Camera::Update(float deltaTime) {
  // Smooth zoom interpolation
  Zoom = Zoom + (TargetZoom - Zoom) * 5.0f * deltaTime;
}

glm::mat4 Camera::GetViewMatrix() {
  // We want to pivot around 'Position'
  // The camera is located at Position - Front * Zoom
  glm::vec3 eye = Position - Front * Zoom;
  return glm::lookAt(eye, Position, Up);
}

glm::mat4 Camera::GetProjectionMatrix(float width, float height) {
  return glm::perspective(glm::radians(55.0f), width / height, 1.0f, 100000.0f);
}

void Camera::updateCameraVectors() {
  glm::vec3 front;
  front.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
  front.y = sin(glm::radians(Pitch));
  front.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
  Front = glm::normalize(front);

  Right = glm::normalize(glm::cross(Front, WorldUp));
  Up = glm::normalize(glm::cross(Right, Front));
}

void Camera::SetAngles(float yaw, float pitch) {
  Yaw = yaw;
  Pitch = pitch;
  updateCameraVectors();
}

void Camera::SaveState(const std::string &filename) {
  std::ofstream outFile(filename);
  if (outFile.is_open()) {
    outFile << Position.x << " " << Position.y << " " << Position.z << "\n";
    outFile << Yaw << " " << Pitch << "\n";
    outFile << TargetZoom << "\n";
    outFile.close();
    std::cout << "[Camera] State saved to " << filename << std::endl;
  }
}

void Camera::LoadState(const std::string &filename) {
  std::ifstream inFile(filename);
  if (inFile.is_open()) {
    inFile >> Position.x >> Position.y >> Position.z;
    inFile >> Yaw >> Pitch;
    inFile >> TargetZoom;

    // Force default MU settings always (per user request: "only default")
    Yaw = -45.0f;
    Pitch = -48.5f;
    TargetZoom = 800.0f;

    Zoom = TargetZoom;
    inFile.close();
    updateCameraVectors();
    std::cout << "[Camera] State loaded from " << filename << std::endl;
  }
}
