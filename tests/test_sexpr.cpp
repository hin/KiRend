#include "sexpr/sexpr.h"

#include <gtest/gtest.h>

using namespace kirend::sexpr;

TEST( Sexpr, ParsesNestedLists )
{
    Node root = Parse( "(a (b 1 2.5) (c \"x y\") sym)" );

    EXPECT_EQ( root.keyword(), "a" );
    ASSERT_NE( root.find( "b" ), nullptr );
    EXPECT_DOUBLE_EQ( root.find( "b" )->number( 1 ), 1.0 );
    EXPECT_DOUBLE_EQ( root.find( "b" )->number( 2 ), 2.5 );
    EXPECT_EQ( root.value( "c" ).value(), "x y" );
    EXPECT_TRUE( root.hasFlag( "sym" ) );
    EXPECT_FALSE( root.hasFlag( "other" ) );
}


TEST( Sexpr, HandlesEscapesAndDataBlocks )
{
    Node root = Parse( "(t \"a\\\"b\\\\c\" (data |AB\n  CD|))" );

    EXPECT_EQ( root.atom( 1 ), "a\"b\\c" );
    EXPECT_EQ( root.value( "data" ).value(), "AB\n  CD" );
}


TEST( Sexpr, ReportsErrorsWithLine )
{
    try
    {
        Parse( "(a\n(b\n" );
        FAIL() << "expected an exception";
    }
    catch( const ParseError& e )
    {
        EXPECT_GE( e.line(), 2 );
    }
}


TEST( Sexpr, ParsesNumbersLocaleIndependently )
{
    EXPECT_DOUBLE_EQ( ToNumber( "-1.25" ).value(), -1.25 );
    EXPECT_DOUBLE_EQ( ToNumber( "+3" ).value(), 3.0 );
    EXPECT_FALSE( ToNumber( "1,5" ).has_value() );
    EXPECT_FALSE( ToNumber( "abc" ).has_value() );
}


TEST( Sexpr, BoolValues )
{
    Node root = Parse( "(x (a yes) (b no) (c))" );
    EXPECT_TRUE( root.boolValue( "a" ).value() );
    EXPECT_FALSE( root.boolValue( "b" ).value() );
    EXPECT_TRUE( root.boolValue( "c" ).value() );
    EXPECT_FALSE( root.boolValue( "d" ).has_value() );
}
