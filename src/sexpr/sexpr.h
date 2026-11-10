// Minimal S-expression parser for KiCad files.
#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace kirend::sexpr {

class ParseError : public std::runtime_error
{
public:
    ParseError( const std::string& aMessage, int aLine );
    int line() const { return m_line; }

private:
    int m_line;
};

/// A node is either an atom (symbol, number or string) or a list of nodes.
struct Node
{
    enum class Kind { Atom, List };

    Kind              kind = Kind::List;
    std::string       text;     ///< Atom text (unquoted for strings, raw for |data| blocks).
    bool              quoted = false;
    std::vector<Node> children; ///< List items. children[0] is usually the keyword.
    int               line = 0;

    bool isAtom() const { return kind == Kind::Atom; }
    bool isList() const { return kind == Kind::List; }

    /// Keyword of a list, i.e. the text of its first atom, or "" if none.
    const std::string& keyword() const;

    /// First child list with the given keyword, or nullptr.
    const Node* find( std::string_view aKeyword ) const;

    /// All child lists with the given keyword.
    std::vector<const Node*> findAll( std::string_view aKeyword ) const;

    /// Text of child atom aIndex (0 = keyword). Throws if missing or not an atom.
    const std::string& atom( size_t aIndex ) const;

    /// Child atom aIndex as a number. Throws if missing or not numeric.
    double number( size_t aIndex ) const;

    /// True if a child atom (index >= 1) equals aFlag, e.g. (pad ... locked).
    bool hasFlag( std::string_view aFlag ) const;

    /// Value of a (key value) child: e.g. node.value("layer") for (layer "F.Cu").
    std::optional<std::string> value( std::string_view aKeyword ) const;

    /// Numeric value of a (key number) child.
    std::optional<double> numberValue( std::string_view aKeyword ) const;

    /// Value of a (key yes|no) child. A bare (key) counts as yes.
    std::optional<bool> boolValue( std::string_view aKeyword ) const;
};

/// Parse a complete document. Returns the single top-level list.
Node Parse( std::string_view aText );

/// Parse a number with the "C" locale.
std::optional<double> ToNumber( std::string_view aText );

} // namespace kirend::sexpr
