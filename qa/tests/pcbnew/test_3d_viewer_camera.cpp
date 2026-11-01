/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <boost/test/unit_test.hpp>

#include <3d_rendering/track_ball.h>


namespace
{
SFVEC3F pointAtCursor( const CAMERA& aCamera, const wxSize& aSize, const wxPoint& aCursor )
{
    const glm::vec4 viewport( 0.0f, 0.0f, aSize.x, aSize.y );
    const float     depth =
            glm::project( aCamera.GetLookAtPos(), aCamera.GetViewMatrix(), aCamera.GetProjectionMatrix(), viewport ).z;

    return glm::unProject( SFVEC3F( aCursor.x, aSize.y - aCursor.y, depth ), aCamera.GetViewMatrix(),
                           aCamera.GetProjectionMatrix(), viewport );
}


void checkCursorPosition( const CAMERA& aCamera, const wxSize& aSize, const SFVEC3F& aPoint, const wxPoint& aExpected )
{
    const SFVEC3F screen = glm::project( aPoint, aCamera.GetViewMatrix(), aCamera.GetProjectionMatrix(),
                                         glm::vec4( 0.0f, 0.0f, aSize.x, aSize.y ) );

    BOOST_CHECK_SMALL( screen.x - aExpected.x, 0.05f );
    BOOST_CHECK_SMALL( aSize.y - screen.y - aExpected.y, 0.05f );
}
} // namespace


BOOST_AUTO_TEST_SUITE( Viewer3DCamera )


BOOST_AUTO_TEST_CASE( ZoomKeepsCursorAnchor )
{
    for( PROJECTION_TYPE projection : { PROJECTION_TYPE::PERSPECTIVE, PROJECTION_TYPE::ORTHO } )
    {
        for( const wxSize& size : { wxSize( 800, 600 ), wxSize( 1600, 1200 ), wxSize( 1001, 751 ) } )
        {
            for( const wxPoint& cursor : { wxPoint( 0, 0 ), wxPoint( size.x - 1, size.y - 1 ),
                                           wxPoint( size.x / 2, size.y / 2 ), wxPoint( size.x / 4, size.y / 3 ) } )
            {
                TRACK_BALL camera( 10.0f );
                camera.SetCurWindowSize( size );

                if( projection == PROJECTION_TYPE::ORTHO )
                    camera.ToggleProjection();

                camera.RotateX( 0.4f );
                camera.RotateY( -0.7f );
                camera.Pan( SFVEC3F( 0.8f, -0.5f, 0.0f ) );
                const SFVEC3F anchor = pointAtCursor( camera, size, cursor );
                const SFVEC3F initialPosition = camera.GetCameraPos();

                BOOST_REQUIRE( camera.ZoomAt( 1.1f, cursor ) );
                checkCursorPosition( camera, size, anchor, cursor );
                BOOST_REQUIRE( camera.ZoomAt( 1.0f / 1.1f, cursor ) );
                checkCursorPosition( camera, size, anchor, cursor );
                BOOST_CHECK_SMALL( glm::length( camera.GetCameraPos() - initialPosition ), 0.0001f );
            }
        }
    }
}


BOOST_AUTO_TEST_CASE( CenterOnCursorAfterZoom )
{
    const wxSize  size( 1001, 751 );
    const wxPoint cursor( 163, 587 );
    const wxPoint center( size.x / 2, size.y / 2 );

    for( PROJECTION_TYPE projection : { PROJECTION_TYPE::PERSPECTIVE, PROJECTION_TYPE::ORTHO } )
    {
        TRACK_BALL camera( 10.0f );
        camera.SetCurWindowSize( size );

        if( projection == PROJECTION_TYPE::ORTHO )
            camera.ToggleProjection();

        camera.RotateY( 0.6f );
        camera.Pan( SFVEC3F( 0.3f, -0.2f, 0.0f ) );
        const SFVEC3F anchor = pointAtCursor( camera, size, cursor );
        const SFVEC3F pivot = camera.GetLookAtPos();

        BOOST_REQUIRE( camera.ZoomAt( 1.1f, cursor ) );
        camera.SetCurMousePosition( cursor );
        camera.Pan( center );
        checkCursorPosition( camera, size, anchor, center );
        BOOST_CHECK_SMALL( glm::length( camera.GetLookAtPos() - pivot ), 0.0001f );
    }
}


BOOST_AUTO_TEST_CASE( ZoomLimitsDoNotMoveAnchor )
{
    const wxSize  size( 800, 600 );
    const wxPoint cursor( 623, 137 );

    for( PROJECTION_TYPE projection : { PROJECTION_TYPE::PERSPECTIVE, PROJECTION_TYPE::ORTHO } )
    {
        TRACK_BALL camera( 10.0f );
        camera.SetCurWindowSize( size );
        camera.SetMinZoom( 0.95f );
        camera.SetMaxZoom( 1.05f );

        if( projection == PROJECTION_TYPE::ORTHO )
            camera.ToggleProjection();

        const SFVEC3F anchor = pointAtCursor( camera, size, cursor );

        BOOST_REQUIRE( camera.ZoomAt( 1.1f, cursor ) );
        checkCursorPosition( camera, size, anchor, cursor );
        const SFVEC3F atMinimum = camera.GetCameraPos();
        BOOST_CHECK( !camera.ZoomAt( 1.1f, cursor ) );
        BOOST_CHECK_SMALL( glm::length( camera.GetCameraPos() - atMinimum ), 0.0001f );

        BOOST_REQUIRE( camera.ZoomAt( 0.5f, cursor ) );
        checkCursorPosition( camera, size, anchor, cursor );
        const SFVEC3F atMaximum = camera.GetCameraPos();
        BOOST_CHECK( !camera.ZoomAt( 0.5f, cursor ) );
        BOOST_CHECK_SMALL( glm::length( camera.GetCameraPos() - atMaximum ), 0.0001f );
    }
}


BOOST_AUTO_TEST_SUITE_END()
