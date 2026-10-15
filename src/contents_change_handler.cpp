#include "contents_change_handler.h"

#include <algorithm>

#include "character.h"
#include "debug.h"
#include "item.h"
#include "item_pocket.h"
#include "json.h"

void contents_change_handler::add_unsealed( item &container )
{
    if( std::ranges::find( unsealed, &container ) == unsealed.end() ) {
        unsealed.emplace_back( &container );
    }
}

void contents_change_handler::unseal_pocket_containing( item &contained )
{
    item *const parent = contained.parent_item();
    if( parent != nullptr ) {
        item_pocket *const pocket = parent->contained_where( contained );
        if( pocket ) {
            // on_contents_changed restacks the pocket and should be called later
            // in Character::handle_contents_changed
            pocket->unseal();
        } else {
            debugmsg( "parent container does not contain item" );
        }
        parent->on_contents_changed();
        add_unsealed( *parent );
    }
}

void contents_change_handler::handle_by( Character &guy )
{
    // some containers could have been destroyed by e.g. monster attack
    unsealed.erase( std::remove_if( unsealed.begin(), unsealed.end(),
    []( const safe_reference<item> &container ) -> bool {
        return !container;
    } ), unsealed.end() );
    guy.handle_contents_changed( unsealed );
    unsealed.clear();
}

void contents_change_handler::serialize( JsonOut &jsout ) const
{
    jsout.write( unsealed );
}

void contents_change_handler::deserialize( JsonIn &jsin )
{
    jsin.read( unsealed );
}
