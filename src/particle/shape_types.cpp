#include "particle/shape_types.hpp"

namespace metaagent::particle {

core::String ShapeDefinition::get_shape_display_name() const
{
	switch (shape_type)
	{
	case ShapeType::ImageSilhouette: return "ImageSilhouette";
	case ShapeType::SplinePath: return "SplinePath";
	case ShapeType::MeshSilhouette: return "MeshSilhouette";
	case ShapeType::SquareGrid:
	default: return "SquareGrid";
	}
}

ImageSamplingMode ShapeDefinition::sanitize_image_sampling_mode(const ImageSamplingMode mode)
{
	switch (mode)
	{
	case ImageSamplingMode::GrayscaleDensity:
	case ImageSamplingMode::SobelEdges:
		return mode;
	case ImageSamplingMode::FilledSilhouette:
	default:
		return ImageSamplingMode::GrayscaleDensity;
	}
}

core::String ShapeDefinition::get_image_sampling_display_name() const
{
	switch (sanitize_image_sampling_mode(image_sampling_mode))
	{
	case ImageSamplingMode::GrayscaleDensity: return "GrayscaleDensity";
	case ImageSamplingMode::SobelEdges:
	default: return "SobelEdges";
	}
}

} // namespace metaagent::particle
