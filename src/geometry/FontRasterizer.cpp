#include "FontRasterizer.h"

namespace pepr3d {

std::vector<std::vector<FontRasterizer::Tri>> FontRasterizer::rasterizeText(const std::string textString,
                                                                            const size_t fontHeight,
                                                                            const size_t bezierSteps) const {
    FT_Set_Char_Size(mFace, static_cast<FT_F26Dot6>(fontHeight << 6), static_cast<FT_F26Dot6>(fontHeight << 6), 96, 96);

    std::vector<std::vector<Tri>> trianglesPerLetter;

    double offset = 0;
    for(size_t i = 0; i < textString.size(); i++) {
        trianglesPerLetter.push_back({});
        offset = addOneCharacter(mFace, textString[i], bezierSteps, offset, trianglesPerLetter.back());
    }

    // postprocess by offsetting y-axis to positive numbers
    outlinePostprocess(trianglesPerLetter);

    return trianglesPerLetter;
}

void FontRasterizer::outlinePostprocess(std::vector<std::vector<Tri>>& trianglesPerLetter) const {
    float min = std::numeric_limits<float>::max();

    // Find the minimum of all y-axis of all triangles of all letters
    for(const auto& letterTriangles : trianglesPerLetter) {
        for(const auto& t : letterTriangles) {
            if(min > t.a.y) {
                min = t.a.y;
            }
            if(min > t.b.y) {
                min = t.b.y;
            }
            if(min > t.c.y) {
                min = t.c.y;
            }
        }
    }

    // Add its abs to push all values into non-negative
    const float addToZero = abs(min);
    for(auto& letterTriangles : trianglesPerLetter) {
        for(auto& t : letterTriangles) {
            t.a.y += addToZero;
            t.b.y += addToZero;
            t.c.y += addToZero;
        }
    }

    /// Uncomment the following part if you need the outline, not a triangulation

    // double min = std::numeric_limits<double>::max();
    //// Find the minimum of all y-axis of all holes and outlines
    // for(auto& cs : outCircumference) {
    //    for(auto& p : cs) {
    //        if(min > p.y) {
    //            min = p.y;
    //        }
    //    }
    //}

    //// Add its abs to push all values into non-negative
    // const double addToZero = abs(min);
    // for(auto& cs : outCircumference) {
    //    for(auto& p : cs) {
    //        p.y += static_cast<float>(addToZero);
    //    }
    //}
}

std::vector<p2t::Point*> FontRasterizer::triangulateContour(Vectoriser* vectoriser, int c, float offset) {
    std::vector<p2t::Point*> polyline;
    const Contour* contour = vectoriser->GetContour(c);
    for(size_t p = 0; p < contour->PointCount(); ++p) {
        const double* d = contour->GetPoint(p);
        polyline.push_back(new p2t::Point((d[0] / 64.0f) + offset, d[1] / 64.0f));
    }
    return polyline;
}

double FontRasterizer::addOneCharacter(const FT_Face face, const char& ch, const size_t bezierSteps, double offset,
                                       std::vector<Tri>& outTriangles) const {
    // float extrude = 0;
    static FT_UInt prevCharIndex = 0, curCharIndex = 0;
    static FT_Pos prev_rsb_delta = 0;

    std::vector<Tri> trianglesLetter;

    curCharIndex = FT_Get_Char_Index(face, ch);
    if(FT_Load_Glyph(face, curCharIndex, FT_LOAD_DEFAULT))
        printf("FT_Load_Glyph failed\n");

    FT_Glyph glyph;
    if(FT_Get_Glyph(face->glyph, &glyph))
        printf("FT_Get_Glyph failed\n");

    if(glyph->format != FT_GLYPH_FORMAT_OUTLINE) {
        printf("Invalid Glyph Format\n");
        exit(0);
    }

    short nCountour = 0;
    nCountour = face->glyph->outline.n_contours;

    if(FT_HAS_KERNING(face) && prevCharIndex) {
        FT_Vector kerning;
        FT_Get_Kerning(face, prevCharIndex, curCharIndex, FT_KERNING_DEFAULT, &kerning);
        offset += kerning.x >> 6;
    }

    if(prev_rsb_delta - face->glyph->lsb_delta >= 32)
        offset -= 1.0f;
    else if(prev_rsb_delta - face->glyph->lsb_delta < -32)
        offset += 1.0f;

    prev_rsb_delta = face->glyph->rsb_delta;

    Vectoriser* vectoriser = new Vectoriser(face->glyph, static_cast<unsigned short>(bezierSteps));
    for(size_t c = 0; c < vectoriser->ContourCount(); ++c) {
        const Contour* contour = vectoriser->GetContour(c);

        /// Uncomment the following part if you need the outline, not a triangulation
        /// Beware, the order of the outlines seems to be random - the first (0) is NOT always the outermost
        /// Depends on the font.

        // outCircumference.push_back({});
        // std::vector<glm::vec2>& circumPart = outCircumference.back();
        //// Add the circumference
        // for(size_t p = 0; p < contour->PointCount(); ++p) {
        //    const double* d1 = contour->GetPoint(p);
        //    circumPart.push_back(glm::vec2((static_cast<float>(d1[0]) / 64.0f) + static_cast<float>(offset),
        //                                    -1 * static_cast<float>(d1[1]) / 64.0f));
        //}

        // Calc the triangulation
        if(contour->GetDirection()) {
            std::vector<p2t::Point*> polyline =
                triangulateContour(vectoriser, static_cast<int>(c), static_cast<float>(offset));
            p2t::CDT* cdt = new p2t::CDT(polyline);

            for(size_t cm = 0; cm < vectoriser->ContourCount(); ++cm) {
                const Contour* sm = vectoriser->GetContour(cm);
                if(c != cm && !sm->GetDirection() && sm->IsInside(contour)) {
                    std::vector<p2t::Point*> pl =
                        triangulateContour(vectoriser, static_cast<int>(cm), static_cast<float>(offset));
                    cdt->AddHole(pl);
                }
            }

            cdt->Triangulate();
            std::vector<p2t::Triangle*> ts = cdt->GetTriangles();
            for(size_t i = 0; i < ts.size(); i++) {
                p2t::Triangle* ot = ts[i];
                Tri t1;
                t1.a.x = static_cast<float>(ot->GetPoint(0)->x);
                t1.a.y = static_cast<float>(-ot->GetPoint(0)->y);
                t1.a.z = static_cast<float>(0.0f);
                t1.b.x = static_cast<float>(ot->GetPoint(1)->x);
                t1.b.y = static_cast<float>(-ot->GetPoint(1)->y);
                t1.b.z = static_cast<float>(0.0f);
                t1.c.x = static_cast<float>(ot->GetPoint(2)->x);
                t1.c.y = static_cast<float>(-ot->GetPoint(2)->y);
                t1.c.z = static_cast<float>(0.0f);
                trianglesLetter.push_back(t1);

                // std::cout << " ,Created: (" << t1.a.x << ", " << t1.a.y << ") to (" << t1.b.x << ", " << t1.b.y  <<
                // ") to (" << t1.c.x << ", " << t1.c.y << ")\n";
            }
            delete cdt;
        }
    }

    delete vectoriser;
    vectoriser = NULL;

    prevCharIndex = curCharIndex;
    double chSize = static_cast<double>(face->glyph->advance.x >> 6);
    outTriangles = std::move(trianglesLetter);
    return offset + chSize;
}

}  // namespace pepr3d