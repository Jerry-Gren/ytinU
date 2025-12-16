#include "transform.h"

void Transform::setRotation(const glm::vec3 &eulerDegrees)
{
    rotationEuler = eulerDegrees;
    // 将度数转为弧度，再转为四元数
    rotation = glm::quat(glm::radians(rotationEuler));
}

void Transform::setRotation(const glm::quat &quat)
{
    rotation = quat;
    // 从四元数反推欧拉角 (度数)
    rotationEuler = glm::degrees(glm::eulerAngles(rotation));
}

void Transform::setFromTRS(const glm::mat4 &trs)
{
    // https://blog.csdn.net/hunter_wwq/article/details/21473519
    position = {trs[3][0], trs[3][1], trs[3][2]};
    scale = {glm::length(trs[0]), glm::length(trs[1]), glm::length(trs[2])};
    glm::quat newRot = glm::quat_cast(glm::mat3(
        glm::vec3(trs[0][0], trs[0][1], trs[0][2]) / scale[0],
        glm::vec3(trs[1][0], trs[1][1], trs[1][2]) / scale[1],
        glm::vec3(trs[2][0], trs[2][1], trs[2][2]) / scale[2]));
    setRotation(newRot);
}

void Transform::lookAt(const glm::vec3 &target, const glm::vec3 &up)
{
    glm::quat newRot = glm::quatLookAt(glm::normalize(target - position), up);
    setRotation(newRot);
}

glm::vec3 Transform::getFront() const
{
    return rotation * getDefaultFront();
}

glm::vec3 Transform::getUp() const
{
    return rotation * getDefaultUp();
}

glm::vec3 Transform::getRight() const
{
    return rotation * getDefaultRight();
}

glm::mat4 Transform::getLocalMatrix() const
{
    return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale);
}