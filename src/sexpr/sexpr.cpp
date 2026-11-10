#include "sexpr/sexpr.h"

#include <charconv>
#include <cstdlib>
#include <cstring>

namespace kirend::sexpr {

ParseError::ParseError( const std::string& aMessage, int aLine ) :
        std::runtime_error( "line " + std::to_string( aLine ) + ": " + aMessage ),
        m_line( aLine )
{
}


static const std::string s_empty;


const std::string& Node::keyword() const
{
    if( isList() && !children.empty() && children[0].isAtom() )
        return children[0].text;

    return s_empty;
}


const Node* Node::find( std::string_view aKeyword ) const
{
    for( const Node& child : children )
    {
        if( child.isList() && child.keyword() == aKeyword )
            return &child;
    }

    return nullptr;
}


std::vector<const Node*> Node::findAll( std::string_view aKeyword ) const
{
    std::vector<const Node*> result;

    for( const Node& child : children )
    {
        if( child.isList() && child.keyword() == aKeyword )
            result.push_back( &child );
    }

    return result;
}


const std::string& Node::atom( size_t aIndex ) const
{
    if( aIndex >= children.size() || !children[aIndex].isAtom() )
    {
        throw ParseError( "expected value " + std::to_string( aIndex ) + " in (" + keyword() + ")",
                          line );
    }

    return children[aIndex].text;
}


double Node::number( size_t aIndex ) const
{
    const std::string& text = atom( aIndex );

    if( std::optional<double> value = ToNumber( text ) )
        return *value;

    throw ParseError( "expected number in (" + keyword() + "), got '" + text + "'", line );
}


bool Node::hasFlag( std::string_view aFlag ) const
{
    for( size_t i = 1; i < children.size(); ++i )
    {
        if( children[i].isAtom() && !children[i].quoted && children[i].text == aFlag )
            return true;
    }

    return false;
}


std::optional<std::string> Node::value( std::string_view aKeyword ) const
{
    const Node* child = find( aKeyword );

    if( !child || child->children.size() < 2 || !child->children[1].isAtom() )
        return std::nullopt;

    return child->children[1].text;
}


std::optional<double> Node::numberValue( std::string_view aKeyword ) const
{
    std::optional<std::string> text = value( aKeyword );

    if( !text )
        return std::nullopt;

    return ToNumber( *text );
}


std::optional<bool> Node::boolValue( std::string_view aKeyword ) const
{
    const Node* child = find( aKeyword );

    if( !child )
        return std::nullopt;

    if( child->children.size() < 2 )
        return true;

    const std::string& text = child->children[1].text;
    return text == "yes" || text == "true";
}


std::optional<double> ToNumber( std::string_view aText )
{
    if( aText.empty() )
        return std::nullopt;

    // std::from_chars for double is locale independent but doesn't accept a leading '+'.
    if( aText.front() == '+' )
        aText.remove_prefix( 1 );

    double value = 0.0;
    auto [ptr, ec] = std::from_chars( aText.data(), aText.data() + aText.size(), value );

    if( ec != std::errc() || ptr != aText.data() + aText.size() )
        return std::nullopt;

    return value;
}


namespace {

class Parser
{
public:
    explicit Parser( std::string_view aText ) : m_text( aText ) {}

    Node parseDocument()
    {
        skipSpace();

        if( atEnd() || m_text[m_pos] != '(' )
            throw ParseError( "document does not start with '('", m_line );

        Node root = parseList();
        return root;
    }

private:
    bool atEnd() const { return m_pos >= m_text.size(); }

    void skipSpace()
    {
        while( !atEnd() )
        {
            char c = m_text[m_pos];

            if( c == '\n' )
            {
                ++m_line;
                ++m_pos;
            }
            else if( c == ' ' || c == '\t' || c == '\r' )
            {
                ++m_pos;
            }
            else if( c == '#' && ( m_pos == 0 || m_text[m_pos - 1] == '\n' ) )
            {
                // Comment line (not produced by KiCad, but tolerated).
                while( !atEnd() && m_text[m_pos] != '\n' )
                    ++m_pos;
            }
            else
            {
                break;
            }
        }
    }

    Node parseList()
    {
        // Iterative parsing with an explicit stack keeps deep files from overflowing the C++ stack.
        std::vector<Node> stack;
        Node              first;
        first.kind = Node::Kind::List;
        first.line = m_line;
        ++m_pos; // '('
        stack.push_back( std::move( first ) );

        while( true )
        {
            skipSpace();

            if( atEnd() )
                throw ParseError( "unexpected end of file, missing ')'", m_line );

            char c = m_text[m_pos];

            if( c == '(' )
            {
                Node list;
                list.kind = Node::Kind::List;
                list.line = m_line;
                ++m_pos;
                stack.push_back( std::move( list ) );
            }
            else if( c == ')' )
            {
                ++m_pos;
                Node done = std::move( stack.back() );
                stack.pop_back();

                if( stack.empty() )
                    return done;

                stack.back().children.push_back( std::move( done ) );
            }
            else
            {
                stack.back().children.push_back( parseAtom() );
            }
        }
    }

    Node parseAtom()
    {
        Node atom;
        atom.kind = Node::Kind::Atom;
        atom.line = m_line;

        char c = m_text[m_pos];

        if( c == '"' )
        {
            atom.quoted = true;
            ++m_pos;

            while( true )
            {
                if( atEnd() )
                    throw ParseError( "unterminated string", atom.line );

                char ch = m_text[m_pos++];

                if( ch == '"' )
                    break;

                if( ch == '\n' )
                    ++m_line;

                if( ch == '\\' && !atEnd() )
                {
                    char esc = m_text[m_pos++];

                    switch( esc )
                    {
                    case 'n': atom.text += '\n'; break;
                    case 't': atom.text += '\t'; break;
                    case 'r': atom.text += '\r'; break;
                    case '"': atom.text += '"'; break;
                    case '\\': atom.text += '\\'; break;
                    default:
                        atom.text += '\\';
                        atom.text += esc;
                        break;
                    }

                    continue;
                }

                atom.text += ch;
            }
        }
        else if( c == '|' )
        {
            // |...| raw data block (embedded files). Whitespace inside is kept as is.
            ++m_pos;
            size_t end = m_text.find( '|', m_pos );

            if( end == std::string_view::npos )
                throw ParseError( "unterminated |data| block", atom.line );

            std::string_view raw = m_text.substr( m_pos, end - m_pos );

            for( char ch : raw )
            {
                if( ch == '\n' )
                    ++m_line;
            }

            atom.text.assign( raw );
            atom.quoted = true;
            m_pos = end + 1;
        }
        else
        {
            size_t start = m_pos;

            while( !atEnd() )
            {
                char ch = m_text[m_pos];

                if( ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '(' || ch == ')' )
                    break;

                ++m_pos;
            }

            atom.text.assign( m_text.substr( start, m_pos - start ) );
        }

        return atom;
    }

    std::string_view m_text;
    size_t           m_pos = 0;
    int              m_line = 1;
};

} // namespace


Node Parse( std::string_view aText )
{
    Parser parser( aText );
    return parser.parseDocument();
}

} // namespace kirend::sexpr
