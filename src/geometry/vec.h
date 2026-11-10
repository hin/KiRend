// Small vector / matrix helpers (double precision).
#pragma once

#include <array>
#include <cmath>

namespace kirend {

constexpr double PI = 3.14159265358979323846;

inline double DegToRad( double aDeg ) { return aDeg * PI / 180.0; }

struct Vec2
{
    double x = 0.0;
    double y = 0.0;

    Vec2() = default;
    constexpr Vec2( double aX, double aY ) : x( aX ), y( aY ) {}

    Vec2 operator+( const Vec2& o ) const { return { x + o.x, y + o.y }; }
    Vec2 operator-( const Vec2& o ) const { return { x - o.x, y - o.y }; }
    Vec2 operator*( double s ) const { return { x * s, y * s }; }
    Vec2 operator/( double s ) const { return { x / s, y / s }; }
    bool operator==( const Vec2& o ) const = default;

    double length() const { return std::hypot( x, y ); }
};

inline double Dot( const Vec2& a, const Vec2& b ) { return a.x * b.x + a.y * b.y; }
inline double Cross( const Vec2& a, const Vec2& b ) { return a.x * b.y - a.y * b.x; }

/// Rotate a point the way KiCad does in board coordinates (Y axis down, angle in degrees,
/// positive = counter-clockwise on screen).
inline Vec2 RotateKiCad( const Vec2& aPoint, double aAngleDeg )
{
    if( aAngleDeg == 0.0 )
        return aPoint;

    const double a = DegToRad( aAngleDeg );
    const double c = std::cos( a );
    const double s = std::sin( a );
    return { aPoint.x * c + aPoint.y * s, aPoint.y * c - aPoint.x * s };
}

struct Vec3
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    Vec3() = default;
    constexpr Vec3( double aX, double aY, double aZ ) : x( aX ), y( aY ), z( aZ ) {}

    Vec3 operator+( const Vec3& o ) const { return { x + o.x, y + o.y, z + o.z }; }
    Vec3 operator-( const Vec3& o ) const { return { x - o.x, y - o.y, z - o.z }; }
    Vec3 operator-() const { return { -x, -y, -z }; }
    Vec3 operator*( double s ) const { return { x * s, y * s, z * s }; }
    Vec3 operator/( double s ) const { return { x / s, y / s, z / s }; }
    Vec3& operator+=( const Vec3& o ) { x += o.x; y += o.y; z += o.z; return *this; }
    bool operator==( const Vec3& o ) const = default;

    double length() const { return std::sqrt( x * x + y * y + z * z ); }
};

inline double Dot( const Vec3& a, const Vec3& b ) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Vec3 Cross( const Vec3& a, const Vec3& b )
{
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

inline Vec3 Normalize( const Vec3& v )
{
    double len = v.length();
    return len > 0.0 ? v / len : v;
}

/// Column-major 4x4 matrix, same layout and multiplication order as glm::mat4.
struct Mat4
{
    // m[col][row]
    std::array<std::array<double, 4>, 4> m{};

    static Mat4 Identity()
    {
        Mat4 r;
        for( int i = 0; i < 4; ++i )
            r.m[i][i] = 1.0;
        return r;
    }

    Mat4 operator*( const Mat4& b ) const
    {
        Mat4 r;
        for( int c = 0; c < 4; ++c )
            for( int row = 0; row < 4; ++row )
            {
                double sum = 0.0;
                for( int k = 0; k < 4; ++k )
                    sum += m[k][row] * b.m[c][k];
                r.m[c][row] = sum;
            }
        return r;
    }

    Vec3 transformPoint( const Vec3& p ) const
    {
        return { m[0][0] * p.x + m[1][0] * p.y + m[2][0] * p.z + m[3][0],
                 m[0][1] * p.x + m[1][1] * p.y + m[2][1] * p.z + m[3][1],
                 m[0][2] * p.x + m[1][2] * p.y + m[2][2] * p.z + m[3][2] };
    }

    Vec3 transformVector( const Vec3& v ) const
    {
        return { m[0][0] * v.x + m[1][0] * v.y + m[2][0] * v.z,
                 m[0][1] * v.x + m[1][1] * v.y + m[2][1] * v.z,
                 m[0][2] * v.x + m[1][2] * v.y + m[2][2] * v.z };
    }

    /// Equivalent of glm::translate( *this, v ).
    Mat4 translated( const Vec3& v ) const { return *this * Translation( v ); }

    /// Equivalent of glm::rotate( *this, angle, axis ).
    Mat4 rotated( double aAngleRad, const Vec3& aAxis ) const
    {
        return *this * Rotation( aAngleRad, aAxis );
    }

    /// Equivalent of glm::scale( *this, v ).
    Mat4 scaled( const Vec3& v ) const
    {
        Mat4 s = Identity();
        s.m[0][0] = v.x;
        s.m[1][1] = v.y;
        s.m[2][2] = v.z;
        return *this * s;
    }

    static Mat4 Translation( const Vec3& v )
    {
        Mat4 t = Identity();
        t.m[3][0] = v.x;
        t.m[3][1] = v.y;
        t.m[3][2] = v.z;
        return t;
    }

    static Mat4 Rotation( double aAngleRad, const Vec3& aAxis )
    {
        const Vec3   a = Normalize( aAxis );
        const double c = std::cos( aAngleRad );
        const double s = std::sin( aAngleRad );
        const Vec3   t = a * ( 1.0 - c );

        Mat4 r = Identity();
        r.m[0][0] = c + t.x * a.x;
        r.m[0][1] = t.x * a.y + s * a.z;
        r.m[0][2] = t.x * a.z - s * a.y;
        r.m[1][0] = t.y * a.x - s * a.z;
        r.m[1][1] = c + t.y * a.y;
        r.m[1][2] = t.y * a.z + s * a.x;
        r.m[2][0] = t.z * a.x + s * a.y;
        r.m[2][1] = t.z * a.y - s * a.x;
        r.m[2][2] = c + t.z * a.z;
        return r;
    }

    /// Inverse of a rigid/affine transform (general 4x4 inverse not needed).
    Mat4 inverseAffine() const;

    /// True if the 3x3 part flips handedness (negative determinant).
    bool mirrors() const
    {
        double det = m[0][0] * ( m[1][1] * m[2][2] - m[2][1] * m[1][2] )
                     - m[1][0] * ( m[0][1] * m[2][2] - m[2][1] * m[0][2] )
                     + m[2][0] * ( m[0][1] * m[1][2] - m[1][1] * m[0][2] );
        return det < 0.0;
    }
};

inline Mat4 Mat4::inverseAffine() const
{
    // Invert the 3x3 part, then the translation.
    const auto& a = m;
    double det = a[0][0] * ( a[1][1] * a[2][2] - a[2][1] * a[1][2] )
                 - a[1][0] * ( a[0][1] * a[2][2] - a[2][1] * a[0][2] )
                 + a[2][0] * ( a[0][1] * a[1][2] - a[1][1] * a[0][2] );
    double inv = 1.0 / det;

    Mat4 r = Identity();
    r.m[0][0] = ( a[1][1] * a[2][2] - a[2][1] * a[1][2] ) * inv;
    r.m[1][0] = -( a[1][0] * a[2][2] - a[2][0] * a[1][2] ) * inv;
    r.m[2][0] = ( a[1][0] * a[2][1] - a[2][0] * a[1][1] ) * inv;
    r.m[0][1] = -( a[0][1] * a[2][2] - a[2][1] * a[0][2] ) * inv;
    r.m[1][1] = ( a[0][0] * a[2][2] - a[2][0] * a[0][2] ) * inv;
    r.m[2][1] = -( a[0][0] * a[2][1] - a[2][0] * a[0][1] ) * inv;
    r.m[0][2] = ( a[0][1] * a[1][2] - a[1][1] * a[0][2] ) * inv;
    r.m[1][2] = -( a[0][0] * a[1][2] - a[1][0] * a[0][2] ) * inv;
    r.m[2][2] = ( a[0][0] * a[1][1] - a[1][0] * a[0][1] ) * inv;

    Vec3 t( a[3][0], a[3][1], a[3][2] );
    Vec3 it = r.transformVector( t );
    r.m[3][0] = -it.x;
    r.m[3][1] = -it.y;
    r.m[3][2] = -it.z;
    return r;
}

} // namespace kirend
