#pragma once

#include <glm/gtx/hash.hpp>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>
#include <array>


struct Vertex {
	glm::vec3 pos;
	glm::vec3 color;
	glm::vec2 texCoord;

	bool operator==(const Vertex& other) const;

	static VkVertexInputBindingDescription getBindingDescription();

	static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions();

};

namespace std {
	template<> struct hash<Vertex> {
		size_t operator()(Vertex const& vertex) const {
			return ((hash<glm::vec3>()(vertex.pos) ^
				(hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^
				(hash<glm::vec2>()(vertex.texCoord) << 1);
		}
	};
}