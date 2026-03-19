// PDFPlotter.cpp
#include "pch.h"
#include "PDFPlotter.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>

#include "HersheyTextBuilder.h"

static inline float Clamp01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static inline void PdfMoveTo(std::ostringstream& ss, float x, float y)
{
    ss << std::fixed << std::setprecision(3) << x << " " << y << " m\n";
}

static inline void PdfLineTo(std::ostringstream& ss, float x, float y)
{
    ss << std::fixed << std::setprecision(3) << x << " " << y << " l\n";
}

static inline void PdfStroke(std::ostringstream& ss)
{
    ss << "S\n";
}

static inline void PdfSetStrokeColor(std::ostringstream& ss, const glm::vec4& c)
{
    ss << std::fixed << std::setprecision(4)
       << Clamp01(c.r) << " " << Clamp01(c.g) << " " << Clamp01(c.b) << " RG\n";
}

static inline void PdfSetLineWidth(std::ostringstream& ss, float w)
{
    ss << std::fixed << std::setprecision(3) << w << " w\n";
}

static inline void PdfSetLineCapsJoins(std::ostringstream& ss)
{
    // round caps/joins for nicer plots
    ss << "1 J\n"; // round join
    ss << "1 j\n"; // round cap
}

bool PDFPlotter::Write(const std::string& outPath,
    const std::vector<Entity>& entities,
    const PageSettings& page)
{
    const float pageWPt = page.widthIn * 72.0f;
    const float pageHPt = page.heightIn * 72.0f;
    const float dpiX = std::max(1.0f, page.dpiX);

    // Build content stream
    std::ostringstream content;
    content << "% VectorKernel PDFPlotter\n";
    content << "q\n";
    // Clip to page bounds
    content << std::fixed << std::setprecision(3)
            << 0.0f << " " << 0.0f << " " << pageWPt << " " << pageHPt << " re W n\n";
    PdfSetLineCapsJoins(content);

    auto emitLine = [&](const LineEntity& l)
    {
        // Convert inches (top-left origin, y down) -> points (bottom-left origin, y up)
        const float x0 = l.start.x * 72.0f;
        const float y0 = (page.heightIn - l.start.y) * 72.0f;
        const float x1 = l.end.x * 72.0f;
        const float y1 = (page.heightIn - l.end.y) * 72.0f;

        // Approximate: treat LineEntity::width as "pixels" at page.dpiX.
        float wPt = l.width * (72.0f / dpiX);
        if (!std::isfinite(wPt) || wPt <= 0.0f)
            wPt = 0.5f;
        wPt = std::max(0.10f, wPt);

        PdfSetStrokeColor(content, l.color);
        PdfSetLineWidth(content, wPt);
        PdfMoveTo(content, x0, y0);
        PdfLineTo(content, x1, y1);
        PdfStroke(content);
    };

    for (const Entity& e : entities)
    {
        if (e.screenSpace)
            continue;
        if (e.tag == EntityTag::Cursor || e.tag == EntityTag::Hud)
            continue;

        switch (e.type)
        {
        case EntityType::Line:
            emitLine(e.line);
            break;

        case EntityType::Text:
        {
            std::vector<LineEntity> textLines;
            textLines.reserve(e.text.text.size() * 8);
            HersheyTextBuilder::BuildLines(e.text, textLines);
            for (const auto& tl : textLines)
                emitLine(tl);
        }
        break;
        }
    }

    content << "Q\n";

    const std::string contentStr = content.str();

    // Helper to write objects and track byte offsets
    std::ostringstream pdf;
    pdf << "%PDF-1.4\n";
    pdf << "%\xE2\xE3\xCF\xD3\n";

    std::vector<long long> offsets;
    offsets.push_back(0); // object 0 unused

    auto writeObj = [&](int objNum, const std::string& body)
    {
        offsets.resize(std::max<size_t>(offsets.size(), (size_t)objNum + 1));
        offsets[objNum] = (long long)pdf.tellp();
        pdf << objNum << " 0 obj\n";
        pdf << body;
        pdf << "\nendobj\n";
    };

    // 1: Catalog
    writeObj(1, "<< /Type /Catalog /Pages 2 0 R >>");

    // 2: Pages
    writeObj(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");

    // 3: Single page
    {
        std::ostringstream ss;
        ss << "<< /Type /Page /Parent 2 0 R";
        ss << " /MediaBox [0 0 " << std::fixed << std::setprecision(3) << pageWPt << " " << pageHPt << "]";
        ss << " /Contents 4 0 R";
        ss << " >>";
        writeObj(3, ss.str());
    }

    // 4: Contents stream
    {
        std::ostringstream ss;
        ss << "<< /Length " << contentStr.size() << " >>\n";
        ss << "stream\n";
        ss << contentStr;
        ss << "endstream";
        writeObj(4, ss.str());
    }

    const long long xrefPos = (long long)pdf.tellp();
    pdf << "xref\n";
    pdf << "0 " << offsets.size() << "\n";
    pdf << "0000000000 65535 f \n";
    for (size_t i = 1; i < offsets.size(); ++i)
    {
        pdf << std::setw(10) << std::setfill('0') << offsets[i] << " 00000 n \n";
    }

    pdf << "trailer\n";
    pdf << "<< /Size " << offsets.size() << " /Root 1 0 R >>\n";
    pdf << "startxref\n";
    pdf << xrefPos << "\n";
    pdf << "%%EOF\n";

    std::ofstream out(outPath, std::ios::binary);
    if (!out)
        return false;
    const std::string pdfStr = pdf.str();
    out.write(pdfStr.data(), (std::streamsize)pdfStr.size());
    return out.good();
}
