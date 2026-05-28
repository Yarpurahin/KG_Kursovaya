#include "Render.h"
#include "GUItextRectangle.h"

#include <windows.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <cmath>
#include <cstring>


#ifdef _DEBUG
#include <Debugapi.h>
struct debug_print
{
    template <class C> debug_print& operator<<(const C& a)
    {
        OutputDebugStringA((std::stringstream() << a).str().c_str());
        return *this;
    }
} debout;
#else
struct debug_print
{
    template <class C> debug_print& operator<<(const C& a)
    {
        return *this;
    }
} debout;
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "MyOGL.h"
extern OpenGL gl;
#include "Light.h"
Light light;
#include "Camera.h"
Camera camera;

bool texturing = true;
bool lightning = true;
bool alpha = false;

namespace
{
    const double PI = 3.14159265358979323846;

    const double PRISM_H = 4.0;
    const double SCALE = 0.33;

    const int CYL_N = 40;
    const int ARC_N = 36;

    double A[2] = { -7,  3 };
    double B[2] = { -4,  7 };
    double C[2] = { 2,  6 };
    double D[2] = { 1,  0 };
    double E[2] = { 6, -4 };
    double F[2] = { 3, -7 };
    double G[2] = { 0, -2 };
    double Hh[2] = { -4, -3 };

    double M[2] = { -5,  0 };

    double uvMinX = 0.0;
    double uvMinY = 0.0;
    double uvSide = 1.0;

    struct Vec3
    {
        double x, y, z;
    };

    struct UV
    {
        double u, v;
    };

    void V(double x, double y, double z)
    {
        glVertex3d(x, y, z);
    }

    double Dist(double x1, double y1, double x2, double y2)
    {
        double dx = x2 - x1;
        double dy = y2 - y1;
        return std::sqrt(dx * dx + dy * dy);
    }

    Vec3 Normalize(const Vec3& v)
    {
        double l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (l < 1e-9)
            return { 0.0, 0.0, 1.0 };
        return { v.x / l, v.y / l, v.z / l };
    }

    void DrawNormalLine(const Vec3& center, const Vec3& normal, double len = 1.8)
    {
        Vec3 n = Normalize(normal);

        glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_LINE_BIT);

        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_BLEND);

        glLineWidth(2.0f);
        glColor3d(1.0, 0.0, 0.0);

        glBegin(GL_LINES);
        V(center.x, center.y, center.z);
        V(center.x + n.x * len, center.y + n.y * len, center.z + n.z * len);
        glEnd();

        glPopAttrib();
    }

    Vec3 SideNormal(double x1, double y1, double x2, double y2)
    {
        double dx = x2 - x1;
        double dy = y2 - y1;
        return Normalize({ -dy, dx, 0.0 });
    }

    bool CircleBy3Points(
        double x1, double y1,
        double x2, double y2,
        double x3, double y3,
        double& ox, double& oy, double& R)
    {
        double a1 = x2 - x1;
        double b1 = y2 - y1;
        double c1 = (a1 * (x1 + x2) + b1 * (y1 + y2)) * 0.5;

        double a2 = x3 - x1;
        double b2 = y3 - y1;
        double c2 = (a2 * (x1 + x3) + b2 * (y1 + y3)) * 0.5;

        double det = a1 * b2 - a2 * b1;
        if (std::fabs(det) < 1e-9)
            return false;

        ox = (c1 * b2 - c2 * b1) / det;
        oy = (a1 * c2 - a2 * c1) / det;
        R = Dist(ox, oy, x1, y1);

        return true;
    }

    double NormalizeAngle(double a)
    {
        while (a < 0) a += 2.0 * PI;
        while (a >= 2.0 * PI) a -= 2.0 * PI;
        return a;
    }

    bool IsBetweenCCW(double start, double finish, double test)
    {
        if (finish < start) finish += 2.0 * PI;
        if (test < start) test += 2.0 * PI;
        return test >= start && test <= finish;
    }

    bool GetArcData(double& ox, double& oy, double& R, double& start, double& finish)
    {
        if (!CircleBy3Points(Hh[0], Hh[1], M[0], M[1], A[0], A[1], ox, oy, R))
            return false;

        double aH = NormalizeAngle(std::atan2(Hh[1] - oy, Hh[0] - ox));
        double aM = NormalizeAngle(std::atan2(M[1] - oy, M[0] - ox));
        double aA = NormalizeAngle(std::atan2(A[1] - oy, A[0] - ox));

        start = aH;
        finish = aA;

        if (!IsBetweenCCW(start, finish, aM))
            finish -= 2.0 * PI;

        return true;
    }

    void GetArcPoint(int i, double& x, double& y)
    {
        double ox, oy, R, start, finish;

        if (!GetArcData(ox, oy, R, start, finish))
        {
            if (i < ARC_N / 2)
            {
                double t = (double)i / (ARC_N / 2);
                x = Hh[0] + (M[0] - Hh[0]) * t;
                y = Hh[1] + (M[1] - Hh[1]) * t;
            }
            else
            {
                double t = (double)(i - ARC_N / 2) / (ARC_N - ARC_N / 2);
                x = M[0] + (A[0] - M[0]) * t;
                y = M[1] + (A[1] - M[1]) * t;
            }
            return;
        }

        double t = (double)i / ARC_N;
        double ang = start + (finish - start) * t;
        x = ox + R * std::cos(ang);
        y = oy + R * std::sin(ang);
    }

    void GetSemiCylinderPoint(int i, double& x, double& y)
    {
        double mx = (B[0] + C[0]) * 0.5;
        double my = (B[1] + C[1]) * 0.5;

        double dx = C[0] - B[0];
        double dy = C[1] - B[1];
        double len = std::sqrt(dx * dx + dy * dy);

        if (len < 1e-9)
        {
            x = mx;
            y = my;
            return;
        }

        double ux = dx / len;
        double uy = dy / len;

        double nx = -dy / len;
        double ny = dx / len;

        double R = len * 0.5;
        double t = PI * i / CYL_N;

        x = mx + ux * (R * std::cos(t)) + nx * (R * std::sin(t));
        y = my + uy * (R * std::cos(t)) + ny * (R * std::sin(t));
    }

    void UpdateBounds(double x, double y, double& minx, double& maxx, double& miny, double& maxy)
    {
        if (x < minx) minx = x;
        if (x > maxx) maxx = x;
        if (y < miny) miny = y;
        if (y > maxy) maxy = y;
    }

    void ComputeUvSquare()
    {
        double minx = 1e9;
        double maxx = -1e9;
        double miny = 1e9;
        double maxy = -1e9;

        double* pts[] = { A, B, C, D, E, F, G, Hh };
        for (int i = 0; i < 8; ++i)
            UpdateBounds(pts[i][0], pts[i][1], minx, maxx, miny, maxy);

        for (int i = 0; i <= ARC_N; ++i)
        {
            double x, y;
            GetArcPoint(i, x, y);
            UpdateBounds(x, y, minx, maxx, miny, maxy);
        }

        for (int i = 0; i <= CYL_N; ++i)
        {
            double x, y;
            GetSemiCylinderPoint(i, x, y);
            UpdateBounds(x, y, minx, maxx, miny, maxy);
        }

        double w = maxx - minx;
        double h = maxy - miny;
        uvSide = (w > h) ? w : h;

        double padX = (uvSide - w) * 0.5;
        double padY = (uvSide - h) * 0.5;

        uvMinX = minx - padX;
        uvMinY = miny - padY;
    }

    UV MapUV(double x, double y)
    {
        UV t;
        t.u = (x - uvMinX) / uvSide;
        t.v = (y - uvMinY) / uvSide;
        return t;
    }

    void TexVertex(double x, double y, double z)
    {
        UV t = MapUV(x, y);
        glTexCoord2d(t.u, t.v);
        glVertex3d(x, y, z);
    }

    void DrawVerticalQuad(double x1, double y1, double x2, double y2, double r, double g, double b)
    {
        Vec3 n = SideNormal(x1, y1, x2, y2);
        glNormal3d(n.x, n.y, n.z);

        glColor3d(r, g, b);

        glBegin(GL_QUADS);
        V(x1, y1, 0.0);
        V(x2, y2, 0.0);
        V(x2, y2, PRISM_H);
        V(x1, y1, PRISM_H);
        glEnd();

        Vec3 center{
            (x1 + x2) * 0.5,
            (y1 + y2) * 0.5,
            PRISM_H * 0.5
        };

        DrawNormalLine(center, n);
    }

    void DrawFlatSides()
    {
        DrawVerticalQuad(A[0], A[1], B[0], B[1], 0.90, 0.45, 0.35);
        DrawVerticalQuad(C[0], C[1], D[0], D[1], 0.35, 0.65, 0.90);
        DrawVerticalQuad(D[0], D[1], E[0], E[1], 0.40, 0.80, 0.45);
        DrawVerticalQuad(E[0], E[1], F[0], F[1], 0.95, 0.75, 0.30);
        DrawVerticalQuad(F[0], F[1], G[0], G[1], 0.75, 0.45, 0.85);
        DrawVerticalQuad(G[0], G[1], Hh[0], Hh[1], 0.35, 0.85, 0.85);
    }

    void DrawArcSides()
    {
        for (int i = 0; i < ARC_N; ++i)
        {
            double x1, y1, x2, y2;
            GetArcPoint(i, x1, y1);
            GetArcPoint(i + 1, x2, y2);

            Vec3 n = SideNormal(x1, y1, x2, y2);
            glNormal3d(n.x, n.y, n.z);

            glColor3d(0.55, 0.70, 0.95);

            glBegin(GL_QUADS);
            V(x1, y1, 0.0);
            V(x2, y2, 0.0);
            V(x2, y2, PRISM_H);
            V(x1, y1, PRISM_H);
            glEnd();

            if (i % 6 == 0)
            {
                Vec3 center{
                    (x1 + x2) * 0.5,
                    (y1 + y2) * 0.5,
                    PRISM_H * 0.5
                };

                DrawNormalLine(center, n);
            }
        }
    }

    void DrawSemiCylinderSide()
    {
        for (int i = 0; i < CYL_N; ++i)
        {
            double x1, y1, x2, y2;
            GetSemiCylinderPoint(i, x1, y1);
            GetSemiCylinderPoint(i + 1, x2, y2);

            Vec3 n = SideNormal(x1, y1, x2, y2);
            Vec3 outN{ -n.x, -n.y, -n.z };

            glNormal3d(outN.x, outN.y, outN.z);

            glColor3d(0.75, 0.45, 0.25);

            glBegin(GL_QUADS);
            V(x1, y1, 0.0);
            V(x2, y2, 0.0);
            V(x2, y2, PRISM_H);
            V(x1, y1, PRISM_H);
            glEnd();

            if (i % 6 == 0)
            {
                Vec3 center{
                    (x1 + x2) * 0.5,
                    (y1 + y2) * 0.5,
                    PRISM_H * 0.5
                };

                DrawNormalLine(center, outN);
            }
        }
    }

    void DrawBottom()
    {
        glNormal3d(0.0, 0.0, -1.0);

        glColor3d(0.45, 0.45, 0.45);

        glBegin(GL_TRIANGLES);

        V(A[0], A[1], 0); V(C[0], C[1], 0); V(B[0], B[1], 0);
        V(A[0], A[1], 0); V(D[0], D[1], 0); V(C[0], C[1], 0);
        V(A[0], A[1], 0); V(G[0], G[1], 0); V(D[0], D[1], 0);
        V(D[0], D[1], 0); V(G[0], G[1], 0); V(E[0], E[1], 0);
        V(E[0], E[1], 0); V(G[0], G[1], 0); V(F[0], F[1], 0);

        for (int i = 0; i < ARC_N; ++i)
        {
            double x1, y1, x2, y2;
            GetArcPoint(i, x1, y1);
            GetArcPoint(i + 1, x2, y2);

            V(G[0], G[1], 0);
            V(x2, y2, 0);
            V(x1, y1, 0);
        }

        glEnd();

        double mx = (B[0] + C[0]) * 0.5;
        double my = (B[1] + C[1]) * 0.5;

        glBegin(GL_TRIANGLE_FAN);
        V(mx, my, 0.0);
        for (int i = CYL_N; i >= 0; --i)
        {
            double x, y;
            GetSemiCylinderPoint(i, x, y);
            V(x, y, 0.0);
        }
        glEnd();

        DrawNormalLine({ 0.0, 0.0, 0.0 }, { 0.0, 0.0, -1.0 }, 2.0);
    }

    void DrawTop(bool useAlpha, GLuint texId, bool texturingEnabled)
    {
        glNormal3d(0.0, 0.0, 1.0);

        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

        if (texturingEnabled)
        {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, texId);
        }
        else
        {
            glDisable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        if (useAlpha)
        {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            glEnable(GL_COLOR_MATERIAL);
            glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

            glDepthMask(GL_FALSE);

            glColor4d(1.0, 1.0, 1.0, 0.35);
        }
        else
        {
            glDisable(GL_BLEND);

            glEnable(GL_COLOR_MATERIAL);
            glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

            glColor4d(1.0, 1.0, 1.0, 1.0);
        }

        glBegin(GL_TRIANGLES);

        TexVertex(A[0], A[1], PRISM_H); TexVertex(B[0], B[1], PRISM_H); TexVertex(C[0], C[1], PRISM_H);
        TexVertex(A[0], A[1], PRISM_H); TexVertex(C[0], C[1], PRISM_H); TexVertex(D[0], D[1], PRISM_H);
        TexVertex(A[0], A[1], PRISM_H); TexVertex(D[0], D[1], PRISM_H); TexVertex(G[0], G[1], PRISM_H);
        TexVertex(D[0], D[1], PRISM_H); TexVertex(E[0], E[1], PRISM_H); TexVertex(G[0], G[1], PRISM_H);
        TexVertex(E[0], E[1], PRISM_H); TexVertex(F[0], F[1], PRISM_H); TexVertex(G[0], G[1], PRISM_H);

        for (int i = 0; i < ARC_N; ++i)
        {
            double x1, y1, x2, y2;
            GetArcPoint(i, x1, y1);
            GetArcPoint(i + 1, x2, y2);

            TexVertex(G[0], G[1], PRISM_H);
            TexVertex(x1, y1, PRISM_H);
            TexVertex(x2, y2, PRISM_H);
        }

        glEnd();

        double mx = (B[0] + C[0]) * 0.5;
        double my = (B[1] + C[1]) * 0.5;

        glBegin(GL_TRIANGLE_FAN);
        TexVertex(mx, my, PRISM_H);
        for (int i = 0; i <= CYL_N; ++i)
        {
            double x, y;
            GetSemiCylinderPoint(i, x, y);
            TexVertex(x, y, PRISM_H);
        }
        glEnd();

        glBindTexture(GL_TEXTURE_2D, 0);

        if (useAlpha)
        {
            glDepthMask(GL_TRUE);
        }

        DrawNormalLine({ 0.0, 0.0, PRISM_H }, { 0.0, 0.0, 1.0 }, 2.0);

        glColor4d(1.0, 1.0, 1.0, 1.0);
        glDisable(GL_COLOR_MATERIAL);
    }

    void DrawWire()
    {
        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_BLEND);

        glColor3d(0.0, 0.0, 0.0);

        glBegin(GL_LINE_STRIP);
        V(A[0], A[1], 0);
        V(B[0], B[1], 0);
        V(C[0], C[1], 0);
        V(D[0], D[1], 0);
        V(E[0], E[1], 0);
        V(F[0], F[1], 0);
        V(G[0], G[1], 0);
        V(Hh[0], Hh[1], 0);
        for (int i = 0; i <= ARC_N; ++i)
        {
            double x, y;
            GetArcPoint(i, x, y);
            V(x, y, 0);
        }
        glEnd();

        glBegin(GL_LINE_STRIP);
        V(A[0], A[1], PRISM_H);
        V(B[0], B[1], PRISM_H);
        V(C[0], C[1], PRISM_H);
        V(D[0], D[1], PRISM_H);
        V(E[0], E[1], PRISM_H);
        V(F[0], F[1], PRISM_H);
        V(G[0], G[1], PRISM_H);
        V(Hh[0], Hh[1], PRISM_H);
        for (int i = 0; i <= ARC_N; ++i)
        {
            double x, y;
            GetArcPoint(i, x, y);
            V(x, y, PRISM_H);
        }
        glEnd();
    }
}
void switchModes(OpenGL* sender, KeyEventArg arg)
{
    auto key = LOWORD(MapVirtualKeyA(arg.key, MAPVK_VK_TO_CHAR));

    switch (key)
    {
    case 'L':
        lightning = !lightning;
        break;
    case 'T':
        texturing = !texturing;
        break;
    case 'A':
        alpha = !alpha;
        break;
    }
}

GuiTextRectangle text;

GLuint texId;

void initRender()
{
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);

    int x, y, n;

    unsigned char* data = stbi_load("texture.png", &x, &y, &n, 4);

    if (data)
    {
        unsigned char* _tmp = new unsigned char[x * 4];
        for (int i = 0; i < y / 2; ++i)
        {
            std::memcpy(_tmp, data + i * x * 4, x * 4);
            std::memcpy(data + i * x * 4, data + (y - 1 - i) * x * 4, x * 4);
            std::memcpy(data + (y - 1 - i) * x * 4, _tmp, x * 4);
        }
        delete[] _tmp;

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, x, y, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        stbi_image_free(data);
    }

    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    ComputeUvSquare();

    camera.caclulateCameraPos();

    gl.WheelEvent.reaction(&camera, &Camera::Zoom);
    gl.MouseMovieEvent.reaction(&camera, &Camera::MouseMovie);
    gl.MouseLeaveEvent.reaction(&camera, &Camera::MouseLeave);
    gl.MouseLdownEvent.reaction(&camera, &Camera::MouseStartDrag);
    gl.MouseLupEvent.reaction(&camera, &Camera::MouseStopDrag);

    gl.MouseMovieEvent.reaction(&light, &Light::MoveLight);
    gl.KeyDownEvent.reaction(&light, &Light::StartDrug);
    gl.KeyUpEvent.reaction(&light, &Light::StopDrug);

    gl.KeyDownEvent.reaction(switchModes);
    text.setSize(512, 180);

    camera.setPosition(2, 1.5, 1.5);
}

void Render(double delta_time)
{
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_NORMALIZE);

    if (gl.isKeyPressed('F'))
    {
        light.SetPosition(camera.x(), camera.y(), camera.z());
    }

    camera.SetUpCamera();
    light.SetUpLight();

    gl.DrawAxes();

    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);

    if (lightning)
        glEnable(GL_LIGHTING);

    if (texturing)
    {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    if (alpha)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    float amb[] = { 0.35f, 0.35f, 0.35f, 1.f };
    float dif[] = { 0.80f, 0.80f, 0.80f, 1.f };
    float spec[] = { 1.00f, 1.00f, 1.00f, 1.f };
    float sh = 0.2f * 256.f;

    glMaterialfv(GL_FRONT, GL_AMBIENT, amb);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, dif);
    glMaterialfv(GL_FRONT, GL_SPECULAR, spec);
    glMaterialf(GL_FRONT, GL_SHININESS, sh);

    glShadeModel(GL_SMOOTH);

    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    // ============ РИСОВАТЬ ТУТ ============
    glPushMatrix();

    glScaled(SCALE, SCALE, SCALE);
    glTranslated(0.0, 0.0, -2.0);

    DrawBottom();
    DrawFlatSides();
    DrawArcSides();
    DrawSemiCylinderSide();

    if (alpha)
    {
        DrawTop(true, texId, texturing);
    }
    else
    {
        DrawTop(false, texId, texturing);
    }

    DrawWire();

    glPopMatrix();

    glDisable(GL_COLOR_MATERIAL);

    light.DrawLightGizmo();

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();

    glOrtho(0, gl.getWidth() - 1, 0, gl.getHeight() - 1, 0, 1);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    std::wstringstream ss;
    ss << std::fixed << std::setprecision(3) << "T - " << (texturing ? L"[вкл]выкл" : L"вкл[выкл]") << L" текстур\n"
        << "L - " << (lightning ? L"[вкл]выкл" : L"вкл[выкл]") << L" освещение\n"
        << "A - " << (alpha ? L"[вкл]выкл" : L"вкл[выкл]") << L" альфа-наложение\n"
        << L"F - переместить свет в позицию камеры\n"
        << L"G - двигать свет по горизонтали\n"
        << L"G+ЛКМ - двигать свет по вертикали\n"
        << L"Координаты света: (" << std::setw(7) << light.x() << "," << std::setw(7) << light.y() << "," << std::setw(7)
        << light.z() << ")\n"
        << L"Координаты камеры: (" << std::setw(7) << camera.x() << "," << std::setw(7) << camera.y() << ","
        << std::setw(7) << camera.z() << ")\n"
        << L"Параметры камеры: R=" << std::setw(7) << camera.distance() << ", fi1=" << std::setw(7) << camera.fi1()
        << ", fi2=" << std::setw(7) << camera.fi2() << '\n'
        << L"delta_time: " << std::setprecision(5) << delta_time << std::endl;

    text.setPosition(10, gl.getHeight() - 10 - 180);
    text.setText(ss.str().c_str());
    text.Draw();

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}