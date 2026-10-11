/*
	nanosvg lives in one translation unit here: its parser and its rasterizer
	are both header only implementations, and both are C, so they get the
	diagnostics relaxed the way a vendored header needs.
*/

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#define NANOSVG_ALL_COLOR_KEYWORDS
#include <nanosvgrast.h>

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/logger.h"
#include "engine/svg.h"

namespace engine::svg
{
	CDocument::~CDocument()
	{
		Close();
	}

	CDocument::CDocument(CDocument &&Other) noexcept :
		m_pImage(Other.m_pImage), m_Source(std::move(Other.m_Source))
	{
		Other.m_pImage = nullptr;
	}

	CDocument &CDocument::operator=(CDocument &&Other) noexcept
	{
		if(this != &Other)
		{
			Close();
			m_pImage = Other.m_pImage;
			m_Source = std::move(Other.m_Source);
			Other.m_pImage = nullptr;
		}
		return *this;
	}

	void CDocument::Close()
	{
		if(m_pImage != nullptr)
		{
			nsvgDelete(m_pImage);
			m_pImage = nullptr;
		}
		m_Source.clear();
	}

	bool CDocument::LoadFromMemory(std::string Source, const char *pUnits, float Dpi)
	{
		Close();
		if(Source.empty())
			return false;

		/*
			The parser terminates the attribute values in the buffer it is
			handed, so the source is kept as the mutable copy it parsed from:
			Reload() then parses the same bytes again rather than a document
			that the first pass has already edited.
		*/
		NSVGimage *pImage = nsvgParse(Source.data(), pUnits, Dpi);
		if(pImage == nullptr)
			return false;

		m_Source = std::move(Source);
		m_pImage = pImage;
		return true;
	}

	bool CDocument::LoadFromFile(const storage::path &File, const char *pUnits, float Dpi)
	{
		const std::optional<std::string> Text = storage::read_text(File);
		if(!Text.has_value())
		{
			LOG_WARN("svg", "failed to read '%s'", File.string().c_str());
			return false;
		}
		if(!LoadFromMemory(*Text, pUnits, Dpi))
		{
			LOG_WARN("svg", "failed to parse '%s'", File.string().c_str());
			return false;
		}
		LOG_TRACE("svg", "parsed '%s' (%.0fx%.0f units)", File.string().c_str(), m_pImage->width, m_pImage->height);
		return true;
	}

	base::vec2 CDocument::Size() const
	{
		if(m_pImage == nullptr)
			return base::vec2(0.0f, 0.0f);
		return base::vec2(m_pImage->width, m_pImage->height);
	}

	bool CDocument::Rasterize(int Width, int Height, storage::byte_vector &Pixels)
	{
		if(m_pImage == nullptr || Width <= 0 || Height <= 0)
			return false;

		const float SourceWidth = m_pImage->width > 0.0f ? m_pImage->width : 1.0f;
		const float SourceHeight = m_pImage->height > 0.0f ? m_pImage->height : 1.0f;
		// fit, keeping the aspect, and centre what is left over
		const float Scale = std::min(static_cast<float>(Width) / SourceWidth,
			static_cast<float>(Height) / SourceHeight);
		const float TranslateX = (static_cast<float>(Width) - SourceWidth * Scale) * 0.5f;
		const float TranslateY = (static_cast<float>(Height) - SourceHeight * Scale) * 0.5f;

		Pixels.assign(static_cast<std::size_t>(Width) * static_cast<std::size_t>(Height) * 4, std::byte{0});

		NSVGrasterizer *pRasterizer = nsvgCreateRasterizer();
		if(pRasterizer == nullptr)
			return false;
		nsvgRasterize(pRasterizer, m_pImage, TranslateX, TranslateY, Scale,
			reinterpret_cast<unsigned char *>(Pixels.data()), Width, Height, Width * 4);
		nsvgDeleteRasterizer(pRasterizer);
		return true;
	}

	void CDocument::MakeWhite()
	{
		if(m_pImage == nullptr)
			return;
		// the alpha is the document's business, the colour is the theme's
		const auto White = [](unsigned int Color) { return 0xffffff00u | (Color & 0xffu); };
		const auto WhitePaint = [&White](NSVGpaint &Paint) {
			if(Paint.type == NSVG_PAINT_COLOR)
				Paint.color = White(Paint.color);
			else if(Paint.type == NSVG_PAINT_LINEAR_GRADIENT || Paint.type == NSVG_PAINT_RADIAL_GRADIENT)
			{
				if(Paint.gradient == nullptr)
					return;
				for(int i = 0; i < Paint.gradient->nstops; i++)
					Paint.gradient->stops[i].color = White(Paint.gradient->stops[i].color);
			}
		};
		for(NSVGshape *pShape = m_pImage->shapes; pShape != nullptr; pShape = pShape->next)
		{
			WhitePaint(pShape->fill);
			WhitePaint(pShape->stroke);
		}
	}

	int RasterSize(float LogicalSize, float Scale)
	{
		const float Scaled = LogicalSize * Scale;
		if(!(Scaled > 4.0f)) // also catches a NaN
			return 4;
		if(Scaled >= 512.0f)
			return 512;
		return static_cast<int>(std::lround(Scaled));
	}
} // namespace engine::svg
