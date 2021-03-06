#include "isotoxin.h"

//-V:getkey:807
//-V:theme:807


MAKE_CHILD<gui_contact_item_c>::~MAKE_CHILD()
{
    ASSERT(parent);
    get().update_text();
    MODIFY(get()).visible(is_visible);
}

MAKE_CHILD<gui_conversation_header_c>::~MAKE_CHILD()
{
    ASSERT( parent );
    get().update_text();
    MODIFY( get() ).show();
}


gui_contact_item_c::gui_contact_item_c(MAKE_ROOT<gui_contact_item_c> &data) :gui_clist_base_c(data, CIR_DNDOBJ), contact(data.contact)
{
    flags.set(F_VIS_FILTER|F_VIS_GROUP);
    ASSERT(!contact || contact->is_rootcontact());
}

gui_contact_item_c::gui_contact_item_c(MAKE_CHILD<gui_contact_item_c> &data) : gui_clist_base_c(data, data.role), contact(data.contact)
{
    flags.set( F_VIS_FILTER | F_VIS_GROUP );
    if (contact && (CIR_LISTITEM == role || CIR_ME == role))
        if (ASSERT(contact->is_rootcontact()))
        {
            tooltip( DELEGATE(this,tt) );
            contact->gui_item = this;
            if (CIR_ME != role) g_app->new_blink_reason( contact->getkey() ).recalc_unread();
        }
}

gui_contact_item_c::gui_contact_item_c( MAKE_CHILD<gui_conversation_header_c> &data ) : gui_clist_base_c( data, CIR_CONVERSATION_HEAD ), contact( data.contact )
{
    flags.set( F_VIS_FILTER | F_VIS_GROUP );
}

gui_contact_item_c::~gui_contact_item_c()
{
    if (gui)
    {
        gui->delete_event(DELEGATE(this, redraw_now));
        gui->delete_event(DELEGATE(this, stop_typing));
        gui->delete_event(DELEGATE(this, animate_typing));
    }
}

ts::wstr_c gui_contact_item_c::tt()
{
    if (contact && !contact->get_comment().is_empty())
        return from_utf8( contact->get_comment() );
    return ts::wstr_c();
}

/*virtual*/ ts::ivec2 gui_contact_item_c::get_min_size() const
{
    if (CIR_ME == role || CIR_CONVERSATION_HEAD == role)
    {
        return ts::ivec2( GET_THEME_VALUE(mecontactheight) );
    }

    ts::ivec2 subsize(0);
    if (CIR_DNDOBJ == role)
    {
        if (const theme_rect_s *thr = themerect())
            subsize = thr->maxcutborder.lt + thr->maxcutborder.rb;
    }
    return GET_THEME_VALUE(contactheight) - subsize;
}

/*virtual*/ ts::ivec2 gui_contact_item_c::get_max_size() const
{
    ts::ivec2 m = super::get_max_size();
    m.y = get_min_size().y;
    return m;
}

namespace
{

struct leech_edit : public autoparam_i
{
    int sx;
    leech_edit(int sx) :sx(sx)
    {
    }
    /*virtual*/ bool i_leeched( guirect_c &to ) override
    {
        if (autoparam_i::i_leeched(to))
        {
            evt_data_s d;
            sq_evt(SQ_PARENT_RECT_CHANGED, owner->getrid(), d);
            return true;
        }
        return false;
    };
    virtual bool sq_evt(system_query_e qp, RID rid, evt_data_s &data) override
    {
        if (!ASSERT(owner)) return false;
        if (owner->getrid() != rid) return false;

        if (qp == SQ_PARENT_RECT_CHANGING)
        {
            HOLD r(owner->getparent());

            int width = 100 + sx + r.as<gui_contact_item_c>().contact_item_rite_margin();

            ts::ivec2 szmin = owner->get_min_size(); if (szmin.x < width) szmin.y = width;
            ts::ivec2 szmax = owner->get_max_size(); if (szmax.y < width) szmax.y = width;
            r().calc_min_max_by_client_area(szmin, szmax);
            fixrect(data.rectchg.rect, szmin, szmax, data.rectchg.area);
            return false;
        }

        if (qp == SQ_PARENT_RECT_CHANGED)
        {
            ts::ivec2 szmin = owner->get_min_size();
            HOLD r(owner->getparent());
            ts::irect cr = r().get_client_area();

            int width = cr.width() - sx - r.as<gui_contact_item_c>().contact_item_rite_margin() - 5 -
                g_app->preloaded_stuff().confirmb->size.x - g_app->preloaded_stuff().cancelb->size.x;
            if (width < 100) width = 100;
            int height = szmin.y;

            MODIFY(*owner).pos(sx, cr.lt.y + (cr.height()-height) / 2).sizew(width).visible(true);
            return false;
        }
        return false;

    }
};

}

void gui_contact_item_c::created()
{
    switch (role)
    {
        case CIR_DNDOBJ:
            set_theme_rect(CONSTASTR("contact.dnd"), false);
            defaultthrdraw = DTHRO_BORDER | DTHRO_CENTER;
            break;
        case CIR_METACREATE:
        case CIR_LISTITEM:
            set_theme_rect(CONSTASTR("contact"), false);
            defaultthrdraw = DTHRO_BORDER | DTHRO_CENTER;
            break;
        case CIR_ME:
            set_theme_rect(CONSTASTR("contact.me"), false);
            defaultthrdraw = DTHRO_BORDER | DTHRO_CENTER;
            break;
        case CIR_CONVERSATION_HEAD:
            set_theme_rect(CONSTASTR("contact.head"), false);
            defaultthrdraw |= DTHRO_BASE_HOLE;
            break;
    }

    if (const theme_rect_s * thr = themerect())
    {
        ts::ivec2 s = parsevec2(thr->addition.get_string(CONSTASTR("shiftstateicon")), ts::ivec2(0));
        shiftstateicon.x = (ts::int16)s.x;
        shiftstateicon.y = (ts::int16)s.y;
    }

    gui_control_c::created();

    if (CIR_CONVERSATION_HEAD == role)
    {
    }
}


ts::uint32 gui_contact_item_c::gm_handler( gmsg<ISOGM_SELECT_CONTACT> & c )
{
    MEMT( MEMT_CONTACT_ITEM );

    switch (role)
    {
    case CIR_LISTITEM:
    case CIR_METACREATE:
        if (!c.contact)
        {
            TSDEL( this );
            return 0;
        }
    case CIR_ME:
        {
            bool o = getprops().is_active();
            bool n = contact == c.contact;
            if (o != n)
            {
                MODIFY(*this).active(n);
                update_text();

                if (n)
                    g_app->active_contact_item = CIR_ME == role ? nullptr : this;
                else
                {
                    if (prf_options().is(UIOPT_TAGFILETR_BAR))
                        if (!contact->match_tags(prf().bitags()))
                            g_app->recreate_ctls(true, false);
                }

            }
        }
        break;
    case CIR_CONVERSATION_HEAD:
        if (!c.contact) contact = nullptr;
        break;
    }

    return 0;
}

bool gui_contact_item_c::setcontact(contact_root_c *c)
{
    bool changed = contact != c;
    contact = c;
    update_text();
    return changed;
}

void gui_contact_item_c::typing()
{
    flags.set(F_SHOWTYPING);
    DEFERRED_UNIQUE_CALL(1.5, DELEGATE(this, stop_typing), nullptr);
    animate_typing(RID(), nullptr);
}

bool gui_contact_item_c::stop_typing(RID, GUIPARAM)
{
    typing_buf.clear();
    flags.clear(F_SHOWTYPING);
    update_text();
    return true;
}
bool gui_contact_item_c::animate_typing(RID, GUIPARAM)
{
    update_text();
    if (flags.is(F_SHOWTYPING))
        DEFERRED_UNIQUE_CALL(1.0 / 5.0, DELEGATE(this, animate_typing), nullptr);
    return true;
}

void gui_contact_item_c::update_text()
{
    MEMT( MEMT_CONTACT_ITEM_TEXT );

    ts::wstr_c ctext;

    if (contact)
    {
        if (contact->getkey().is_self)
        {
            ts::str_c newtext(prf().username());
            text_adapt_user_input(newtext);
            ts::str_c t2(prf().userstatus());
            text_adapt_user_input(t2);
            if (CIR_CONVERSATION_HEAD == role)
            {
                ts::ivec2 sz = g_app->preloaded_stuff().editb->size;
                newtext.append( CONSTASTR(" <rect=0,") );
                newtext.append_as_uint( sz.x ).append_char(',').append_as_uint( -sz.y ).append(CONSTASTR(",2><br><l>"));

                if ( t2.is_empty() )
                    t2.set( maketag_color<char>( get_default_text_color( COLOR_PROTO_TEXT_OFFLINE ) ) )
                    .append( to_utf8( TTT("no status message",457) ) ).append(CONSTASTR("</color>"));

                newtext.append(t2).append(CONSTASTR(" <rect=1,"));
                newtext.append_as_uint( sz.x ).append_char(',').append_as_uint( -sz.y ).append(CONSTASTR(",2></l>"));
            } else
            {
                newtext.append(CONSTASTR("<br><l>")).append(t2).append(CONSTASTR("</l>"));
            }
            ctext = ts::from_utf8(newtext);
        } else if (CIR_METACREATE == role && contact->subcount() > 1)
        {
            ts::str_c newtext;
            contact->subiterate([&](contact_c *c) {
                newtext.append(CONSTASTR("<nl>"));
                ts::str_c n = c->get_name(false);
                text_adapt_user_input(n);
                newtext.append( n );
                if (active_protocol_c *ap = prf().ap( c->getkey().protoid ))
                    newtext.append(CONSTASTR(" (")).append(ap->get_name()).append(CONSTASTR(")"));
            });
            ctext = ts::from_utf8(newtext);

        } else if (contact->getkey().is_conference())
        {
            ctext = contact->showname();

            if ( contact->get_state() == CS_ONLINE )
            {
                if ( CIR_CONVERSATION_HEAD == role )
                {
                    if (contact->get_conference_permissions() & CP_CHANGE_NAME)
                    {
                        ts::ivec2 sz = g_app->preloaded_stuff().editb->size;
                        ctext.append( CONSTWSTR( " <rect=0," ) );
                        ctext.append_as_uint( sz.x ).append_char( ',' ).append_as_int( -sz.y ).append( CONSTWSTR( ",2>" ) );
                    }
                }
                else
                {
                    ctext.append( CONSTWSTR( "<br>(" ) ).append_as_num( contact->subonlinecount() + 1 ).append_char( '/' );
                    if (auto uc = contact->subunknowncount()) ctext.append_as_num( uc ).append_char( '/' );
                    ctext.append_as_num( contact->subcount() + 1 ).append_char( ')' );
                    if ( active_protocol_c *ap = prf().ap( contact->getkey().protoid ) )
                        ctext.append(CONSTWSTR(" <l>")).append(ts::from_utf8(ap->get_name())).append(CONSTWSTR("</l>"));
                }

                if (contact->get_pubid().is_empty())
                    ctext.append(CONSTWSTR("<br>")).append(TTT("temporary conference", 256));
            } else
                ctext.append(CONSTWSTR("<br>")).append(TTT("inactive conference", 501));

        } else if (contact->is_meta())
        {
            int live = 0, count = 0, rej = 0, invsend = 0, invrcv = 0, wait = 0, deactivated = 0;
            contact->subiterate( [&](contact_c *c) {
                ++count;
                if (c->get_state() != CS_ROTTEN) ++live;
                if (c->get_state() == CS_REJECTED) ++rej;
                if (c->get_state() == CS_INVITE_SEND) ++invsend;
                if (c->get_state() == CS_INVITE_RECEIVE) ++invrcv;
                if (c->get_state() == CS_WAIT) ++wait;
                if (nullptr==prf().ap(c->getkey().protoid)) ++deactivated;
            } );
            if (live == 0)
            {
                DEFERRED_EXECUTION_BLOCK_BEGIN(0)

                    if (contact_key_s *ck = gui->temp_restore<contact_key_s>(as_int(param)))
                        contacts().kill( *ck );

                DEFERRED_EXECUTION_BLOCK_END( gui->temp_store<contact_key_s>(contact->getkey()) )
            } else if (rej > 0)
                ctext = colorize(TTT("Rejected", 79), get_default_text_color(COLOR_TEXT_SPECIAL));
            else if (invsend > 0)
            {
                ctext = contact->showname();
                if (!ctext.is_empty()) ctext.append(CONSTWSTR("<br>"));
                ctext.append( colorize(TTT("Authorization request has been sent",88), get_default_text_color(COLOR_TEXT_SPECIAL)) );
            }
            else {
                ctext = contact->showname();
                ts::str_c t2(contact->get_statusmsg());
                text_adapt_user_input(t2);

                if (CIR_CONVERSATION_HEAD == role)
                {
                    ts::ivec2 sz = g_app->preloaded_stuff().editb->size;
                    ctext.append(CONSTWSTR(" <rect=0,"));
                    ctext.append_as_uint(sz.x).append_char(',').append_as_int(-sz.y).append(CONSTWSTR(",2>"));
                    t2.trim();
                    if (!t2.is_empty()) ctext.append(CONSTWSTR("<br><l>")).append(ts::from_utf8(t2)).append(CONSTWSTR("</l>"));

                } else
                {
                    if (invrcv)
                    {
                        if (!ctext.is_empty()) ctext.append(CONSTWSTR("<br>"));
                        ctext.append( colorize( TTT("Please, accept or reject",153), get_default_text_color(COLOR_TEXT_SPECIAL) ) );
                        t2.clear();

                        g_app->new_blink_reason(contact->getkey()).friend_invite();
                    }

                    if (wait)
                    {
                        if (!ctext.is_empty()) ctext.append(CONSTWSTR("<br>"));
                        ctext.append(colorize(TTT("Waiting...",154), get_default_text_color(COLOR_TEXT_SPECIAL)));
                        t2.clear();
                    } else if (contact->flag_full_search_result && g_app->found_items)
                    {
                        // Simple linear iteration... not so fast, yeah.
                        // But I hope number of found items is not so huge

                        ts::aint cntitems = 0;
                        for (const found_item_s &fi : g_app->found_items->items)
                            if (fi.historian == contact->getkey())
                            {
                                cntitems = fi.utags.count();
                                break;
                            }
                        if (cntitems > 0)
                        {

                            if (!ctext.is_empty()) ctext.append(CONSTWSTR("<br>"));
                            ctext.append(CONSTWSTR("<l>")).
                                append(colorize<ts::wchar>(TTT("Found: $",338)/ts::wmake(cntitems), get_default_text_color(COLOR_TEXT_FOUND))).
                                append(CONSTWSTR("</l>"));
                            t2.clear();
                        }

                    } else if (flags.is(F_SHOWTYPING))
                    {
                        ts::wstr_c t,b;
                        typing_buf.split(t,b,'\1');
                        ts::wstr_c ins(CONSTWSTR("<fullheight><i>"));
                        t = text_typing( t, b, ins );
                        typing_buf.set(t).append_char('\1').append(b);
                        if (!ctext.is_empty()) ctext.append(CONSTWSTR("<br>"));

                        ctext.append(colorize(t.as_sptr(), get_default_text_color(COLOR_TEXT_TYPING)));
                        t2.clear();
                    }

                    if (count == 1 && deactivated > 0)
                    {
                        t2.clear();
                    }
                    t2.trim();
                    if (!t2.is_empty()) ctext.append(CONSTWSTR("<br><l>")).append(ts::from_utf8(t2)).append(CONSTWSTR("</l>"));

                    if (g_app->F_SHOW_CONTACTS_IDS())
                    {
                        ts::wstr_c ids; ids.set_as_char('[').append_as_int(contact->getkey().contactid).append(CONSTWSTR("] "));
                        ctext.insert(0, ids);
                    }
                }
            }

        } else
            ctext.set( CONSTWSTR("<error>") );


    } else
    {
        ctext.clear();
    }
    textrect.set_text_only(ctext, false);

    getengine().redraw();

}

/*virtual*/ void gui_contact_item_c::update_dndobj(guirect_c *donor)
{
    update_text();
}

void gui_contact_item_c::target(bool tgt)
{
    if (tgt)
        set_theme_rect(CONSTASTR("contact.dndtgt"), false);
    else
        set_theme_rect(CONSTASTR("contact"), false);
}

void gui_contact_item_c::on_drop(gui_contact_item_c *ondr)
{
    if (contact->getkey().is_conference())
    {
        contact->join_conference( ondr->contact );
        return;
    }

    if (dialog_already_present(UD_METACONTACT)) return;

    SUMMON_DIALOG<dialog_metacontact_c>(UD_METACONTACT, true, dialog_metacontact_params_s(contact->getkey()));

    gmsg<ISOGM_METACREATE> mca(ondr->contact->getkey());
    mca.state = gmsg<ISOGM_METACREATE>::ADD;
    mca.send();

    getengine().redraw();
}

/*virtual*/ guirect_c * gui_contact_item_c::summon_dndobj(const ts::ivec2 &deltapos)
{
    flags.set( F_DNDDRAW );
    gui_contact_item_c &dnd = MAKE_ROOT<gui_contact_item_c>(contact);
    ts::ivec2 subsize(0);
    if (const theme_rect_s *thr = dnd.themerect())
        subsize = thr->maxcutborder.lt + thr->maxcutborder.rb;
    dnd.update_text();
    MODIFY(dnd).pos( getprops().screenpos() + deltapos ).size( getprops().size() - subsize ).visible(true);
    return &dnd;
}

int gui_contact_item_c::contact_item_rite_margin() const
{
    return 5;
}

bool gui_contact_item_c::allow_drag() const
{
    if (!allow_drop())
        return false;

    if (CIR_LISTITEM == role && !contact->getkey().is_conference())
    {
        gmsg<ISOGM_METACREATE> mca(contact->getkey());
        mca.state = gmsg<ISOGM_METACREATE>::CHECKINLIST;
        if (!mca.send().is(GMRBIT_ACCEPTED))
            return true;;
    }

    return false;
}

bool gui_contact_item_c::allow_drop() const
{
    if ( contact->is_system_user )
        return false;

    if (contact->getkey().is_conference() && contact->get_state() == CS_OFFLINE)
        return false;

    int failcount = 0;
    contact->subiterate([&](contact_c *c) {
        if (c->get_state() == CS_INVITE_SEND) ++failcount;
        if (c->get_state() == CS_INVITE_RECEIVE) ++failcount;
        if (c->get_state() == CS_WAIT) ++failcount;
        if (nullptr == prf().ap(c->getkey().protoid)) ++failcount;
    });
    if (failcount > 0)
        return false;

    return true;
}

/*virtual*/ bool gui_contact_item_c::sq_evt(system_query_e qp, RID rid, evt_data_s &data)
{
    MEMT( MEMT_CONTACT_ITEM );

    if (rid != getrid())
    {
        // from submenu
        if (popupmenu && popupmenu->getrid() == rid)
        {
            if (SQ_POPUP_MENU_DIE == qp)
                MODIFY(*this).highlight(false);
        }
        return false;
    }


    switch ( qp )
    {
    case SQ_DRAW:
        if ( !prf().is_loaded() )
            return gui_control_c::sq_evt( qp, rid, data );

        if ( flags.is( F_DNDDRAW ) )
        {
            if ( gui->dragndrop_underproc() == this )
                return true;
            flags.clear( F_DNDDRAW );
        }
        gui_control_c::sq_evt( qp, rid, data );
        if ( m_engine && contact )
        {
            ts::irect ca = get_client_area();

            contact_state_e st = CS_OFFLINE;
            contact_online_state_e ost = COS_ONLINE;
            if ( contact )
            {
                st = contact->get_meta_state();
                ost = contact->get_meta_ostate();
                if ( role == CIR_CONVERSATION_HEAD )
                    st = contact_state_check;
            }
            const theme_image_s *state_icon = g_app->preloaded_stuff().offline;
            bool force_state_icon = CIR_ME == role || CIR_METACREATE == role;
            switch ( st )
            {
            case CS_INVITE_SEND:
                state_icon = g_app->preloaded_stuff().invite_send;
                force_state_icon = true;
                break;
            case CS_INVITE_RECEIVE:
                state_icon = g_app->preloaded_stuff().invite_recv;
                force_state_icon = true;
                break;
            case CS_REJECTED:
                state_icon = g_app->preloaded_stuff().invite_rej;
                force_state_icon = true;
                break;
            case CS_ONLINE:
                if ( ost < ARRAY_SIZE( g_app->preloaded_stuff().online ) )
                    state_icon = g_app->preloaded_stuff().online[ ost ];
                else
                    state_icon = g_app->preloaded_stuff().online[ COS_DND ];
                break;
            case CS_WAIT:
                state_icon = nullptr;
                break;
            case CS_ROTTEN:
            case CS_OFFLINE:
                if ( CIR_ME == role )
                    if ( contact->subcount() == 0 )
                        state_icon = nullptr;
                break;
            case contact_state_check: // some online, some offline
                if ( role == CIR_CONVERSATION_HEAD )
                {
                    state_icon = nullptr;
                }
                else
                {
                    state_icon = g_app->preloaded_stuff().online_some;
                }
                break;
            }

            m_engine->begin_draw();

            if ( !contact->is_system_user && role != CIR_CONVERSATION_HEAD )
            {
                if ( !force_state_icon && ( prf_options().is( UIOPT_PROTOICONS ) || contact->fully_unknown() ) )
                    state_icon = nullptr;

                // draw state
                if ((force_state_icon || st != CS_ROTTEN) && state_icon)
                {
                    state_icon->draw( *m_engine.get(), ca + ts::ivec2( shiftstateicon.x, shiftstateicon.y ), ALGN_RIGHT | ALGN_BOTTOM );

                } else if (contact->is_meta()) // not conference
                {
                    // draw proto icons
                    int isz = GET_THEME_VALUE( protoiconsize );
                    ts::ivec2 p( ca.rb.x, ca.rb.y - isz );
                    contact->subiterate( [&]( contact_c *c ) {
                        if ( active_protocol_c *ap = prf().ap( c->getkey().protoid ) )
                        {
                            p.x -= isz;

                            icon_type_e icot = IT_OFFLINE;
                            if ( c->get_state() == CS_ONLINE )
                            {
                                contact_online_state_e ost1 = c->get_ostate();
                                if ( COS_AWAY == ost1 ) icot = IT_AWAY;
                                else if ( COS_DND == ost1 ) icot = IT_DND;
                                else icot = IT_ONLINE;
                            }
                            else if ( c->get_state() == CS_UNKNOWN )
                                icot = IT_UNKNOWN;

                            m_engine->draw( p, ap->get_icon( isz, icot ).extbody(), true );
                        }
                    } );
                }
            }

            if ( contact->flag_is_av && !contact->flag_full_search_result && CIR_CONVERSATION_HEAD != role )
            {
                if ( const av_contact_s *avc = g_app->avcontacts().find_inprogress_any( contact ) )
                {
                    const theme_image_s *img_voicecall = contact->getkey().is_conference() ? nullptr : gui->theme().get_image( CONSTASTR( "voicecall" ) );
                    const theme_image_s *img_micoff = gui->theme().get_image( CONSTASTR( "micoff" ) );
                    const theme_image_s *img_speakeroff = gui->theme().get_image( CONSTASTR( "speakeroff" ) );
                    const theme_image_s *img_speakeron = contact->getkey().is_conference() ? gui->theme().get_image( CONSTASTR( "speakeron" ) ) : nullptr;
                    const theme_image_s * drawarr[ 3 ];
                    int drawarr_cnt = 0;
                    if ( img_voicecall )
                        drawarr[ drawarr_cnt++ ] = img_voicecall;
                    if ( avc->is_mic_off() && img_micoff )
                        drawarr[ drawarr_cnt++ ] = img_micoff;
                    if ( avc->is_speaker_off() && img_speakeroff )
                        drawarr[ drawarr_cnt++ ] = img_speakeroff;
                    if ( avc->is_speaker_on() && img_speakeron )
                        drawarr[ drawarr_cnt++ ] = img_speakeron;

                    int h = 0;
                    for ( int i = 0; i < drawarr_cnt; ++i )
                    {
                        int hh = drawarr[ i ]->info().sz.y;
                        if ( hh > h ) h = hh;
                    }
                    int addh[ 3 ];
                    for ( int i = 0; i < drawarr_cnt; ++i )
                    {
                        int hh = drawarr[ i ]->info().sz.y;
                        addh[ i ] = ( h - hh ) / 2;
                    }

                    ts::ivec2 p( ca.rt() );
                    for ( int i = 0; i < drawarr_cnt; ++i )
                    {
                        p.x -= drawarr[ i ]->info().sz.x;
                        drawarr[ i ]->draw( *m_engine, ts::ivec2( p.x, p.y + addh[ i ] ) );
                        --p.x;
                    }
                }
            }

            m_engine->end_draw();

            if ( contact )
            {
                text_draw_params_s tdp;

                const application_c::blinking_reason_s * achtung = nullptr;
                int ritem = 0, curpww = 0;
                bool draw_ava = !contact->getkey().is_self;
                bool draw_proto = !contact->getkey().is_conference();
                //bool draw_btn = true;
                if ( CIR_CONVERSATION_HEAD == role )
                {
                    gui_conversation_header_c *ch = ts::ptr_cast<gui_conversation_header_c *>( this );

                    ritem = contact_item_rite_margin();
                    if ( draw_proto )
                    {
                        curpww = ch->prepare_protocols();
                        ritem += curpww;
                    }

                    ts::irect cac( ca );

                    int x_offset = draw_ava ? ( contact->get_avatar() ? g_app->preloaded_stuff().icon[ CSEX_UNKNOWN ]->info().sz.x : g_app->preloaded_stuff().icon[ contact->get_meta_gender() ]->info().sz.x ) : 0;

                    cac.lt.x += x_offset + 5;
                    cac.rb.x -= 5;
                    cac.rb.x -= ritem;
                    int curw = cac.width();
                    int w = gui->textsize( *textrect.font, textrect.get_text() ).x;
                    if ( draw_ava && w > curw )
                    {
                        curw += x_offset;
                        draw_ava = false;
                    }
                    while ( draw_proto && w > curw )
                    {
                        if ( curpww > GET_THEME_VALUE( minprotowidth ) )
                        {
                            int shift = w - curw;
                            if ( shift <= ( curpww - GET_THEME_VALUE( minprotowidth ) ) )
                            {
                                ritem -= curpww;
                                curw += curpww;
                                curpww -= shift;
                                curw -= curpww;
                                ritem += curpww;
                                ASSERT( w <= curw );
                                break;
                            }
                        }

                        curw += curpww;
                        draw_proto = false;
                    }

                }
                else
                {
                    achtung = g_app->find_blink_reason( contact->getkey(), false );
                }


                int x_offset = 0;
                if ( draw_ava )
                {
                    m_engine->begin_draw();
                    if ( const avatar_s *ava = contact->get_avatar() )
                    {
                        int y = ( ca.size().y - ava->info().sz.y ) / 2;
                        m_engine->draw( ca.lt + ts::ivec2( y ), ava->extbody(), ava->alpha_pixels );
                        x_offset = g_app->preloaded_stuff().icon[ CSEX_UNKNOWN ]->info().sz.x;
                    }
                    else
                    {
                        const theme_image_s *icon = contact->getkey().is_conference() ? g_app->preloaded_stuff().conference : g_app->preloaded_stuff().icon[ contact->get_meta_gender() ];
                        icon->draw( *m_engine.get(), ca.lt );
                        x_offset = icon->info().sz.x;
                    }
                    m_engine->end_draw();
                }

                ts::irect noti_draw_area = ca;

                gui_conversation_header_c *ch = CIR_CONVERSATION_HEAD == role ? ts::ptr_cast<gui_conversation_header_c *>( this ) : nullptr;

                if ( ch == nullptr || !ch->edit_mode() )
                {
                    MEMT( MEMT_CONTACT_ITEM_1 );

                    ca.lt += ts::ivec2( x_offset + 5, 2 );
                    ca.rb.x -= 5;
                    if ( CIR_CONVERSATION_HEAD == role )
                    {
                        ca.rb.x -= ritem;
                        if ( !draw_proto )
                            ca.rb.x += curpww;
                    }
                    draw_data_s &dd = m_engine->begin_draw();
                    dd.size = ca.size();
                    if ( dd.size >> 0 )
                    {
                        if ( ch )
                        {
                            ch->last_head_text_pos = ca.lt;
                            tdp.rectupdate = DELEGATE( ch, updrect );
                        }

                        dd.offset += ca.lt;
                        int oldxo = dd.offset.x;
                        ts::flags32_s f; f.setup( ts::TO_VCENTER | ts::TO_LINE_END_ELLIPSIS );
                        tdp.textoptions = &f; //-V506
                        tdp.forecolor = nullptr;
                        draw( dd, tdp );

                        if ( ch && draw_proto )
                        {
                            dd.offset.x = oldxo + dd.size.x + 5;
                            dd.size.x = curpww;
                            ch->draw_online_state_text( dd );
                        }
                    }
                    m_engine->end_draw();
                }
                if ( achtung )
                {
                    draw_data_s &dd = m_engine->begin_draw();

                    auto draw_bg = [&]()
                    {
                        if ( const theme_image_s *bachtung = g_app->preloaded_stuff().achtung_bg )
                        {
                            ts::ivec2 p = GET_THEME_VALUE( achtung_shift ) + noti_draw_area.lb();
                            p.y -= bachtung->info().sz.y;
                            bachtung->draw( *m_engine.get(), p );
                            ts::irect trect = ts::irect::from_center_and_size( p + bachtung->center, bachtung->info().sz );
                            dd.offset += trect.lt;
                            dd.size = bachtung->info().sz;
                            tdp.forecolor = &GET_THEME_VALUE( achtung_content_color );
                        }

                        if ( !achtung->get_blinking() )
                            dd.alpha = 128;
                    };


                    if ( CS_INVITE_RECEIVE != st && achtung->is_file_download() || achtung->flags.is( application_c::blinking_reason_s::F_RINGTONE ) )
                    {
                        draw_bg();

                        const theme_image_s *img = nullptr;
                        if ( achtung->flags.is( application_c::blinking_reason_s::F_RINGTONE ) )
                            img = gui->theme().get_image( CONSTASTR( "achtung_call" ) );
                        else if ( achtung->is_file_download() )
                            img = gui->theme().get_image( CONSTASTR( "achtung_file" ) );

                        img->draw( *m_engine, ( dd.size - img->info().sz ) / 2 );

                    }
                    else if ( CS_INVITE_RECEIVE == st || achtung->unread_count > 0 || achtung->flags.is( application_c::blinking_reason_s::F_NEW_VERSION | application_c::blinking_reason_s::F_INVITE_FRIEND ) )
                    {
                        draw_bg();

                        ts::flags32_s f; f.setup( ts::TO_VCENTER | ts::TO_HCENTER );
                        tdp.textoptions = &f; //-V506

                        if ( CS_INVITE_RECEIVE == st || achtung->flags.is( application_c::blinking_reason_s::F_NEW_VERSION ) || achtung->unread_count == 0 )
                        {
                            m_engine->draw(ts::wstr_c(CONSTWSTR("!")), tdp );
                        }
                        else
                        {
                            int n_unread = achtung->unread_count;
                            if ( n_unread > 99 ) n_unread = 99;
                            m_engine->draw( ts::wstr_c().set_as_uint( n_unread ), tdp );
                        }
                    }

                    m_engine->end_draw();
                }
            }
        }
        return true;
    case SQ_RECT_CHANGED:
        textrect.make_dirty( false, false, true );
        return true;
    case SQ_MOUSE_IN:
        if ( CIR_LISTITEM == role || CIR_METACREATE == role )
        {
            if ( !getprops().is_highlighted() )
            {
                MODIFY( *this ).highlight( true );
                update_text();
            }
        }
        break;
    case SQ_MOUSE_OUT:
    {
        if ( CIR_LISTITEM == role || CIR_METACREATE == role )
        {
            if ( getprops().is_highlighted() && popupmenu == nullptr )
            {
                MODIFY( *this ).highlight( false );
                update_text();
            }
        }
        flags.clear( F_LBDN );
    }
    break;
    case SQ_MOUSE_LDOWN:
        flags.set( F_LBDN );

        if ( allow_drag() )
            gui->dragndrop_lb( this );

        return true;
    case SQ_MOUSE_LUP:
        if ( flags.is( F_LBDN ) )
        {
            if ( CIR_CONVERSATION_HEAD != role )
                gmsg<ISOGM_SELECT_CONTACT>( contact, RSEL_SCROLL_END ).send();

            flags.clear( F_LBDN );
        }
        return false;
    case SQ_MOUSE_L2CLICK:
        if ( CIR_LISTITEM == role && !contact->getkey().is_self )
        {
            if (g_app->F_SPLIT_UI())
            {
                HOLD( g_app->main ).as<mainrect_c>().create_new_conv( contact );

            } else
            {
                contact_key_s ck( contact->getkey() );
                SUMMON_DIALOG<dialog_contact_props_c>( UD_CONTACTPROPS, true, dialog_contactprops_params_s( ck, true ) );
            }
        }
        return false;
    case SQ_MOUSE_MUP:
        if ( CIR_LISTITEM == role && !contact->getkey().is_self && contact->get_avatar() )
        {
            contact_key_s ck;
            bool fdone = false;
            contact->subiterate ( [&](const contact_c *c)
            {
                if ( fdone )
                    return;

                if ( c->is_ava_default && c->get_avatar() )
                {
                    ck = c->getkey();
                    fdone = true;
                    return;
                }
                if ( c->get_avatar() && ( c->is_default || ck.is_empty() ) )
                    ck = c->getkey();
            } );

            if (!ck.is_empty())
            {
                ts::bitmap_c bmp = prf().load_avatar( ck );
                if (bmp.info().sz >> 0)
                    dialog_msgbox_c::mb_avatar( TTT( "Full size avatar of $", 479 ) / from_utf8( contact->get_description() ), bmp ).summon(true);
            }

        }
        return false;
    case SQ_MOUSE_RUP:
        if (CIR_LISTITEM == role && !contact->getkey().is_self)
        {
            struct handlers
            {
                static void m_leave_doit(const ts::str_c&cks)
                {
                    bool keep_leave = false;
                    if (cks.begins( CONSTASTR( "1/" ) ))
                        keep_leave = true;

                    contact_key_s ck(cks.as_sptr().skip(keep_leave ? 2 : 0));
                    if ( active_protocol_c *ap = prf().ap( ck.protoid ) )
                        ap->leave_conference( ck.gidcid(), keep_leave );
                }
                static void m_enter( const ts::str_c&cks )
                {
                    contact_key_s ck( cks );
                    if ( active_protocol_c *ap = prf().ap( ck.protoid ) )
                    {
                        if ( TCT_CONFERENCE == ck.temp_type )
                        {
                            if ( conference_s *c = prf().find_conference_by_id( ck.contactid ) )
                                ap->enter_conference( c->pubid );
                        }
                    }
                }
                static void m_delete_doit( const ts::str_c&cks )
                {
                    contact_key_s ck( cks );
                    contacts().kill( ck );
                }

                static void m_leave( const ts::str_c&cks )
                {
                    contact_key_s ck( cks );
                    contact_c * c = contacts().find( ck );
                    if ( c )
                    {
                        ts::wstr_c txt;
                        if ( c->getkey().is_conference() )
                        {
                            txt = TTT( "Leave conference?[br]$[br]History will not be deleted", 258 ) / from_utf8( c->get_description() );

                            menu_c m;
                            m.add( TTT("Keep inactive",515), MIF_MARKED, MENUHANDLER(), CONSTASTR("1") );

                            dialog_msgbox_c::mb_warning( txt ).bcancel().on_ok( m_leave_doit, cks ).checkboxes(m).summon(true);
                        }
                    }
                }
                static void m_delete(const ts::str_c&cks)
                {
                    contact_key_s ck(cks);
                    contact_c * c = contacts().find(ck);
                    if (c)
                    {
                        ts::wstr_c txt;
                        if ( c->getkey().is_conference() )
                        {
                            txt = TTT("Conference will be deleted:[br]$[br]History will be deleted",502) / from_utf8( c->get_description() );
                        }  else
                            txt = TTT("Contact will be deleted:[br]$",84) / from_utf8(c->get_description());

                        dialog_msgbox_c::mb_warning(txt).bcancel().on_ok(m_delete_doit, cks).summon(true);
                    }
                }
                static void m_invite2c( const ts::str_c&cks )
                {
                    ts::token<char> t( cks, '|' );

                    contact_key_s ck( t->as_sptr() );
                    contact_root_c * c = contacts().rfind( ck );
                    if (c && c->getkey().is_conference())
                    {
                        ++t;
                        contact_key_s cki( t->as_sptr() );
                        contact_root_c * ci = contacts().rfind( cki );
                        c->join_conference( ci );
                    }
                }
                static void m_invite( const ts::str_c&cks )
                {
                    contact_key_s ck( cks );
                    contact_c * c = contacts().find( ck );
                    if ( c )
                    {
                        SUMMON_DIALOG<dialog_addcontact_c>( UD_ADDCONTACT, true, dialog_addcontact_params_s( gui_isodialog_c::title( title_new_contact ), c->get_pubid(), ck ) );
                    }
                }
                static void m_metacontact_detach(const ts::str_c&cks)
                {
                    contact_key_s ck(cks);
                    if (contact_c *c = contacts().find(ck))
                        c->detach();
                }

                static void m_contact_props(const ts::str_c&cks)
                {
                    contact_key_s ck(cks);
                    SUMMON_DIALOG<dialog_contact_props_c>(UD_CONTACTPROPS, true, dialog_contactprops_params_s(ck));
                }
                static void m_contact_tag(const ts::str_c&p)
                {
                    ts::token<char> x(p,'/');
                    contact_key_s ck(x->as_sptr());
                    ++x;
                    if (contact_root_c * c = contacts().rfind(ck))
                        c->toggle_tag( x->as_sptr() );
                }
                static void m_newtag(const ts::str_c&cks)
                {
                    struct newtagmodule_s
                    {
                        contact_key_s ck;
                        newtagmodule_s(const ts::str_c&cks):ck(cks) {}

                        bool ok(const ts::wstr_c &ntags, const ts::str_c &)
                        {
                            if (contact_root_c * c = contacts().rfind(ck))
                            {
                                ts::astrings_c tags;
                                tags.split<char>(to_utf8(ntags), ',');
                                tags.trim();
                                tags.add( c->get_tags() );
                                tags.kill_dups_and_sort(true);
                                c->set_tags(tags);
                                prf().dirtycontact(ck);
                                contacts().rebuild_tags_bits();
                            }


                            TSDEL(this);
                            return true;
                        }
                        bool cancel(RID, GUIPARAM)
                        {
                            TSDEL(this);
                            return true;
                        }

                    } *mdl = TSNEW( newtagmodule_s, cks );


                    SUMMON_DIALOG<dialog_entertext_c>(UD_NEWTAG, true, dialog_entertext_c::params(
                        UD_NEWTAG,
                        gui_isodialog_c::title(title_newtags),
                        ts::wstr_c(TTT("Enter comma separated phrases/words",97)),
                        ts::wstr_c(),
                        ts::str_c(),
                        DELEGATE(mdl, ok),
                        DELEGATE(mdl, cancel),
                        check_always_ok));
                }

                static void m_export_history(const ts::str_c&cks)
                {
                    contact_key_s ck(cks);

                    if (contact_root_c *c = contacts().rfind(ck))
                    {
                        if (c->gui_item)
                        {
                            ts::wstr_c downf = prf().download_folder_prepared(c);
                            ts::make_path(downf, 0);

                            ts::wstr_c n = from_utf8(c->get_name());
                            ts::fix_path(n, FNO_MAKECORRECTNAME);


                            ts::wstrings_c fns, fnsa;
                            ts::g_fileop->find(fns, CONSTWSTR("*.template"), false);

                            ts::tmp_array_inplace_t<ts::extension_s,1> es;

                            for (const ts::wstr_c &tn : fns)
                            {
                                ts::wstrings_c tnn(ts::fn_get_name_with_ext(tn), '.');
                                if (tnn.size() < 2) continue;
                                int exti = 0; if (tnn.size() > 2) exti = 1;
                                ts::wstr_c saveas(tnn.get(0));
                                ts::extension_s &e = es.add(); fnsa.add(tn);
                                e.desc = TTT("As $ file", 326) / saveas;
                                e.ext = tnn.get(exti);
                                e.desc.append(CONSTWSTR(" (*.")).append(e.ext).append_char(')');
                            }

                            ts::extensions_s exts(es.begin(), es.size());
                            exts.index = 0;

                            ts::wstr_c title( TTT("Export history",324) );
                            ts::wstr_c fn = c->gui_item->getroot()->save_filename_dialog(downf, n, exts, title);

                            if (!fn.is_empty() && exts.index >= 0)
                            {
                                c->export_history(fnsa.get(exts.index), fn);
                            }
                        }
                    }

                }

                static void m_clear_history_doit(const ts::str_c&cks)
                {
                    contact_key_s ck(cks);
                    if (contact_root_c *c = contacts().rfind(ck))
                    {
                        c->del_history();
                        c->reselect();
                    }
                }

                static void m_clear_history(const ts::str_c&cks)
                {
                    contact_key_s ck(cks);
                    if (contact_root_c *c = contacts().rfind(ck))
                    {
                        ts::wstr_c txt;
                        txt = TTT("History will be deleted[br]$",530) / from_utf8(c->get_description());
                        dialog_msgbox_c::mb_warning(txt).bcancel().on_ok(m_clear_history_doit, cks).summon(true);
                    }
                }
            };

            if (!dialog_already_present(UD_CONTACTPROPS))
            {
                menu_c m;

                contact->subiterate( [&]( const contact_c *c ) {
                    if (c->is_allow_invite && c->get_state() == CS_UNKNOWN)
                    {
                        m.add( TTT("Send authorization request: $",453) / ts::wstr_c(CONSTWSTR("<b>"), ts::from_utf8(c->get_pubid_desc()), CONSTWSTR("</b>")), 0, handlers::m_invite, c->getkey().as_str() );
                    }
                } );

                if (contact->is_meta() && contact->subcount() > 1)
                {
                    menu_c mc = m.add_sub(TTT("Metacontact",145));
                    contact->subiterate([&](contact_c *c) {

                        ts::str_c text(c->get_name(false));
                        text_adapt_user_input(text);
                        if (active_protocol_c *ap = prf().ap(c->getkey().protoid))
                            text.append(CONSTASTR(" (")).append(ap->get_name()).append(CONSTASTR(")"));
                        mc.add( TTT("Detach: $",197) / from_utf8(text), 0, handlers::m_metacontact_detach, c->getkey().as_str() );
                    });

                }

                if ( contact->getkey().is_conference() )
                {
                    if (active_protocol_c *ap = prf().ap( contact->getkey().protoid ))
                    {
                        if (0 != (ap->get_features() & PF_CONFERENCE_ENTER_LEAVE))
                        {
                            if (contact->get_state() == CS_OFFLINE)
                            {
                                m.add( TTT( "Enter conference", 505 ), 0, handlers::m_enter, contact->getkey().as_str() );
                                m.add_separator();
                            }
                            if (contact->get_state() == CS_ONLINE)
                            {
                                m.add( TTT( "Leave conference", 304 ), 0, handlers::m_leave, contact->getkey().as_str() );

                                ts::tmp_pointers_t< contact_root_c, 4 > inv;

                                contacts().iterate_proto_contacts( [&]( contact_c *c ) {

                                    if (c->getmeta() && !contact->subpresent( c->getkey() ) && c->get_state() == CS_ONLINE) // TODO: protocol support offline invite?
                                        inv.set( c->getmeta() );

                                    return true;
                                }, ap->getid() );

                                if (inv.size())
                                {
                                    menu_c ma = m.add_sub( TTT( "Invite", 516 ) );
                                    for (contact_root_c *c : inv)
                                    {
                                        ts::str_c n = c->get_name();
                                        text_adapt_user_input( n );

                                        ma.add( ts::from_utf8( n ), 0, handlers::m_invite2c, contact->getkey().as_str().append_char( '|' ).append( c->getkey().as_str() ) );
                                    }
                                }
                                m.add_separator();
                            }
                        }
                    }
                }

                m.add(TTT("Properties",225),0,handlers::m_contact_props,contact->getkey().as_str());

                if (prf_options().is(UIOPT_TAGFILETR_BAR))
                {
                    menu_c mtags = m.add_sub(TTT("Tags",46));
                    ts::astrings_c ctags(contact->get_tags());
                    ctags.add(contacts().get_all_tags());
                    ctags.kill_dups_and_sort(true);
                    for (const ts::str_c &t : ctags)
                        mtags.add(ts::from_utf8(t), contact->get_tags().find(t) >= 0 ? MIF_MARKED : 0, handlers::m_contact_tag, contact->getkey().as_str().append_char('/').append(t));
                    if (ctags.size())
                        mtags.add_separator();
                    mtags.add(TTT("Create new tag",146),0,handlers::m_newtag,contact->getkey().as_str());
                }

                ts::wstrings_c fns;
                ts::g_fileop->find(fns, CONSTWSTR("*.template"), false);
                if (fns.size())
                {
                    m.add_separator();
                    m.add(TTT("Export history to file...", 325), 0, handlers::m_export_history, contact->getkey().as_str());
                    m.add(TTT("Clear history",531), 0, handlers::m_clear_history, contact->getkey().as_str());
                }

                m.add_separator();
                m.add( TTT( "Delete", 85 ), 0, handlers::m_delete, contact->getkey().as_str() );

                popupmenu = &gui_popup_menu_c::show(menu_anchor_s(true), m);
                popupmenu->leech(this);
            }

        } else if (CIR_METACREATE == role)
        {
            struct handlers
            {
                static void m_remove(const ts::str_c&cks)
                {
                    contact_key_s ck(cks);
                    contact_c * c = contacts().find(ck);
                    if (c)
                    {
                        gmsg<ISOGM_METACREATE> mca(ck); mca.state = gmsg<ISOGM_METACREATE>::REMOVE; mca.send();
                    }
                }
                static void m_mfirst(const ts::str_c&cks)
                {
                    contact_key_s ck(cks);
                    contact_c * c = contacts().find(ck);
                    if (c)
                    {
                        gmsg<ISOGM_METACREATE> mca(ck); mca.state = gmsg<ISOGM_METACREATE>::MAKEFIRST; mca.send();
                    }
                }
            };

            rectengine_c &pareng = HOLD(getparent()).engine();
            ts::aint nchild = pareng.children_count();


            menu_c m;
            if (nchild > 1 && pareng.get_child(0) != &getengine())
                m.add(TTT("Move to top",151), 0, handlers::m_mfirst, contact->getkey().as_str());
            m.add(TTT("Remove",148), 0, handlers::m_remove, contact->getkey().as_str());
            gui_popup_menu_c::show(menu_anchor_s(true), m);

        } else if (CIR_ME == role)
        {
            menu_c m;
            add_status_items( m );
            gui_popup_menu_c::show(menu_anchor_s(true), m);
        } else if (CIR_CONVERSATION_HEAD == role && !contact->getkey().is_self)
        {
            ts::ptr_cast<gui_conversation_header_c *>( this )->on_rite_click();
        }
        return false;
#ifdef _DEBUG
    case SQ_DEBUG_CHANGED_SOME:
        if ( CIR_ME == role )
        {
            BOK( ts::SSK_F10 );
        }
        break;
#endif // _DEBUG
    }

    return super::sq_evt(qp,rid,data);
}

INLINE int statev(contact_state_e v)
{
    switch (v) //-V719
    {
        case CS_INVITE_RECEIVE:
            return 100;
        case CS_INVITE_SEND:
        case CS_REJECTED:
            return 10;
        case CS_ONLINE:
        case contact_state_check:
            return 50;
        case CS_WAIT:
            return 30;
        case CS_OFFLINE:
            return 1;
    }
    return 0;
}

/*virtual*/ int gui_contact_item_c::proto_sort_factor() const
{
    ts::tmp_tbuf_t<ts::uint16> tprots;

    if (contact->getkey().is_conference())
    {
        if (contact->get_state() == CS_ONLINE)
            tprots.add( contact->getkey().protoid );

    }
    else
    {
        contact->subiterate( [&]( const contact_c *c ) {
            ts::tbuf_t<ts::auint>::default_comparator dc;
            tprots.insert_sorted_uniq( c->getkey().protoid, dc );
        } );
    }

    return static_cast<int>( prf().protogroupsort( tprots.data16(), tprots.count() ) );
}

bool gui_contact_item_c::same_prots( const gui_contact_item_c &itm ) const
{
    if ( contact->subcount() != itm.getcontact().subcount() )
        return false;

    ts::tmp_tbuf_t<ts::uint16> tprots1, tprots2;

    contact->subiterate( [&]( const contact_c *c ) {
        ts::tbuf_t<ts::auint>::default_comparator dc;
        tprots1.insert_sorted_uniq( c->getkey().protoid, dc );
    } );

    itm.getcontact().subiterate( [&]( const contact_c *c ) {
        ts::tbuf_t<ts::auint>::default_comparator dc;
        tprots2.insert_sorted_uniq( c->getkey().protoid, dc );
    } );

    return tprots1 == tprots2;
}

bool gui_contact_item_c::is_after(gui_contact_item_c &ci)
{
    if (!contact || !ci.contact || contact->getkey() == ci.contact->getkey())
        return false; // same not swap

    int mystate = statev(contact->get_meta_state()) + sort_power();
    int otherstate = statev(ci.contact->get_meta_state()) + ci.sort_power();
    if (otherstate > mystate) return true;
    if (otherstate < mystate) return false;

    ts::aint cap1 = contacts().contact_activity_power( contact->getkey() );
    ts::aint cap2 = contacts().contact_activity_power( ci.contact->getkey() );
    if (cap2 == cap1)
        return ci.contact->getkey().contactid > contact->getkey().contactid;
    return cap2 > cap1;
}

bool gui_contact_item_c::redraw_now(RID, GUIPARAM)
{
    update_text();
    getengine().redraw();
    return true;
}

void gui_contact_item_c::redraw(float delay)
{
    DEFERRED_CALL( delay, DELEGATE(this, redraw_now), nullptr );
}









gui_conversation_header_c::~gui_conversation_header_c()
{
    if ( gui )
    {
        gui->delete_event(DELEGATE(this, update_buttons));
        gui->delete_event(DELEGATE(this, deletevcallnow));
        gui->delete_event(DELEGATE(this, createvcallnow));
    }
}

bool gui_conversation_header_c::setcontact( contact_root_c *c )
{
    bool changed = gui_contact_item_c::setcontact( c );
    if ( changed )
        cancel_edit();
    g_app->update_buttons_head();
    return changed;
}

/*virtual*/ void gui_conversation_header_c::update_text()
{
    hstuff_clear();
    gui_contact_item_c::update_text();

    if ( textrect.is_dirty() )
    {
        for ( rbtn::ebutton_s &b : updr )
            b.updated = false, b.dirty = true;
        update_buttons( getrid() );
    }
}

bool gui_conversation_header_c::edit0( RID, GUIPARAM p )
{
    flags.set( F_EDITNAME );
    update_buttons( getrid() );
    return true;
}
bool gui_conversation_header_c::edit1( RID, GUIPARAM )
{
    flags.set( F_EDITSTATUS );
    update_buttons( getrid() );
    return true;
}

void gui_conversation_header_c::hstuff_update()
{
    str.clear();

    contact_root_c *cc = contact;
    if ( !cc )
        return;

    why_this_subget_e why = WTS_ONLY_ONE;
    const contact_c *def = !cc->getkey().is_self ? cc->subget_smart(why) : nullptr;

    struct preproto_s
    {
        contact_c *c;
        tableview_t<active_protocol_s, pt_active_protocol>::row_s *row;
        int sindex;
    };
    ts::tmp_tbuf_t<preproto_s> splist;

    cc->subiterate( [&]( contact_c *c ) {
        if ( auto *row = prf().get_table_active_protocol().find<true>( (int)c->getkey().protoid ) )
        {
            preproto_s &ap = splist.add();
            ap.c = c;
            ap.row = row;
            ap.sindex = 0;
        }
    } );

    splist.q_sort<preproto_s>( []( preproto_s *s1, preproto_s *s2 )->bool
    {
        if ( s1->row->other.sort_factor == s2->row->other.sort_factor )
            return s1->c->getkey().contactid < s2->c->getkey().contactid;
        return s1->row->other.sort_factor < s2->row->other.sort_factor;
    } );

    int maxheight = getprops().size().y;
    if ( const theme_rect_s *thr = themerect() )
        maxheight -= thr->clborder_y();

    str.set( CONSTWSTR( "<p=r>" ) );

    for ( preproto_s &s : splist )
    {
        s.sindex = str.get_length();

        if ( s.c->get_state() == CS_ONLINE )
        {
            str.append( CONSTWSTR( "<nl><shadow>" ) ); appendtag_color( str, get_default_text_color( COLOR_PROTO_TEXT_ONLINE ) );
            if (def == s.c)
            {
                if (why == WTS_MARKED_DEFAULT)
                    str.append(CONSTWSTR(" =<u>"));
                else
                    str.append(CONSTWSTR(" <u>"));
            } else str.append_char(' ');

            str.append( from_utf8( s.row->other.name ) );
            if (def == s.c) str.append(CONSTWSTR("</u>"));
            str.append( CONSTWSTR( "</color></shadow> " ) );
        }
        else
        {
            str.append( CONSTWSTR( "<nl>" ) ); appendtag_color( str, get_default_text_color( COLOR_PROTO_TEXT_OFFLINE ) );
            if (def == s.c)
            {
                if (why == WTS_MARKED_DEFAULT)
                    str.append(CONSTWSTR(" =<u>"));
                else
                    str.append(CONSTWSTR(" <u>"));
            }
            else str.append_char(' ');
            str.append( from_utf8( s.row->other.name ) );
            if (def == s.c) str.append(CONSTWSTR("</u>"));
            str.append( CONSTWSTR( "</color> " ) );
        }
    }

    for ( ; splist.count();)
    {
        size = gui->textsize( ts::g_default_text_font, str );
        if ( size.y <= maxheight )
            break;

        int ii = splist.get( splist.count() - 1 ).sindex;
        splist.remove_fast( splist.count() - 1 );
        str.set_length( ii );
    }

    flags.clear(F_DIRTY);
}

void gui_conversation_header_c::generate_protocols()
{
    if ( CIR_CONVERSATION_HEAD != role ) return;
    if ( !contact || contact->getkey().is_conference() ) return;

    hstuff_update();
}

void gui_conversation_header_c::clearprotocols()
{
    hstuff_clear();
    getengine().redraw();
}

int gui_conversation_header_c::contact_item_rite_margin() const
{
    if ( acall_button )
        return g_app->preloaded_stuff().callb->size.x + 15 + (vcall_button ? (acall_button->getprops().pos().x - vcall_button->getprops().pos().x) : 0);

    int w = 0;

    if ( b1 ) w = b1->get_min_size().x + 5;
    if ( b2 ) w += b2->get_min_size().x + 5;
    if ( b3 ) w += b3->get_min_size().x + 5;

    return w + gui_contact_item_c::contact_item_rite_margin();
}

int gui_conversation_header_c::prepare_protocols()
{
    MEMT( MEMT_CONVERSATION_HEADER );

    if ( flags.is(F_DIRTY) )
        generate_protocols();
    return size.x;
}

void gui_conversation_header_c::set_default_proto( const ts::str_c&cks )
{
    contact_key_s ck( cks );
    bool regen_prots = false;
    contact->subiterate( [&]( contact_c *c ) {
        if ( ck == c->getkey() )
        {
            if ( !c->is_default )
            {
                c->is_default = true;
                prf().dirtycontact( c->getkey() );
                regen_prots = true;

            }
        }
        else
        {
            if ( c->is_default )
            {
                c->is_default = false;
                prf().dirtycontact( c->getkey() );
                regen_prots = true;
            }
        }
    } );

    if ( regen_prots )
        generate_protocols();
    getengine().redraw();
}

void gui_conversation_header_c::on_rite_click()
{
    int curpww = size.x;

    ts::irect ca = get_client_area();
    ca.rb.x -= contact_item_rite_margin();
    ca.lt.x = ca.rb.x - curpww;
    if ( ca.inside( to_local( ts::get_cursor_pos() ) ) )
    {
        menu_c m;

        struct preproto_s
        {
            contact_c *c;
            tableview_t<active_protocol_s, pt_active_protocol>::row_s *row;
        };
        ts::tmp_tbuf_t<preproto_s> splist;

        contact->subiterate( [&]( contact_c *c ) {
            if ( auto *row = prf().get_table_active_protocol().find<true>( (int)c->getkey().protoid ) )
            {
                preproto_s &ap = splist.add();
                ap.c = c;
                ap.row = row;
            }
        } );

        splist.q_sort<preproto_s>( []( preproto_s *s1, preproto_s *s2 )->bool
        {
            if ( s1->row->other.sort_factor == s2->row->other.sort_factor )
                return s1->c->getkey().contactid < s2->c->getkey().contactid;
            return s1->row->other.sort_factor < s2->row->other.sort_factor;
        } );

        const contact_c *def = contact->subget_only_marked_defaul();
        m.add(TTT("Autoselect by last activity",533), def == nullptr ? MIF_MARKED : 0, DELEGATE(this, set_default_proto), ts::asptr());
        m.add_separator();
        for ( const preproto_s &s : splist )
            m.add( from_utf8( s.row->other.name ), def == s.c ? MIF_MARKED : 0, DELEGATE( this, set_default_proto ), s.c->getkey().as_str() );

        gui_popup_menu_c::show( menu_anchor_s( true ), m );
    }

}

void gui_conversation_header_c::draw_online_state_text( draw_data_s &dd )
{
    if ( !str.is_empty() )
    {
        text_draw_params_s tdp;

        ts::flags32_s f; f.setup( ts::TO_VCENTER | ts::TO_LINE_END_ELLIPSIS );
        tdp.textoptions = &f;

        getengine().draw( str, tdp );
    }
}


void gui_conversation_header_c::updrect( const void *, int r, const ts::ivec2 &p )
{
    if ( r >= rbtn::EB_MAX ) return;

    if ( prf().is_loaded() )
    {
        ts::ivec2 newp = root_to_local( p );
        if ( updr[ r ].dirty || newp != updr[ r ].p )
        {
            updr[ r ].p = newp;
            updr[ r ].updated = true;
            DEFERRED_UNIQUE_CALL( 0, DELEGATE( this, update_buttons ), 1 );
        }
    }
}

bool gui_conversation_header_c::deletevcallnow(RID, GUIPARAM)
{
    if (vcall_button)
    {
        vcall_button->iterate_leeches([](sqhandler_i *l) {
            if (leech_at_left_animated_s *a = dynamic_cast<leech_at_left_animated_s *>(l))
                a->reverse();
        });

    }

    return true;
}

bool gui_conversation_header_c::createvcallnow(RID, GUIPARAM prm)
{
    if (vcall_button)
    {
        vcall_button->iterate_leeches([](sqhandler_i *l) {
            if (leech_at_left_animated_s *a = dynamic_cast<leech_at_left_animated_s *>(l))
                a->forward();
        });
        return true;
    }

    if (!vcall_button && acall_button && acall_button->is_enabled())
    {

        int features = 0;
        int features_online = 0;
        bool now_disabled = false;
        contact->subiterate([&](contact_c *c) {
            if (active_protocol_c *ap = prf().ap(c->getkey().protoid))
            {
                int f = ap->get_features();
                features |= f;
                if (c->get_state() == CS_ONLINE) features_online |= f;
                if (c->flag_is_av || c->flag_is_ringtone || c->flag_is_calltone) now_disabled = true;
            }
        });

        if (now_disabled || 0 == (features & PF_VIDEO_CALLS) || 0 == (features_online & PF_AUDIO_CALLS))
        {
        }
        else
        {
            gui_button_c &b_vcall = MAKE_CHILD<gui_button_c>(getrid());
            vcall_button = &b_vcall;
            b_vcall.set_face_getter(BUTTON_FACE_PRELOADED(callvb));
            b_vcall.tooltip(TOOLTIP(TTT("Video call", 532)));
            b_vcall.set_handler(DELEGATE(this, av_call), as_param(1));

            b_vcall.leech(TSNEW(leech_at_left_animated_s, acall_button, 0, 500, as_int(prm) == 2));
            b_vcall.leech(TSNEW(leech_inout_handler_s, DELEGATE(this, avbinout)));

            if (ts::wstrmap_c(cfg().device_camera()).set(CONSTWSTR("id")).equals(CONSTWSTR("off")))
                b_vcall.disable();

            MODIFY(b_vcall).visible(true);
            getengine().redraw();
        }

    }

    return true;
}

bool gui_conversation_header_c::avbinout(RID, GUIPARAM inout)
{
    if (inout)
    {
        // in
        gui->delete_event(DELEGATE(this, deletevcallnow));
        DEFERRED_UNIQUE_CALL(0, DELEGATE(this, createvcallnow), inout);
    }
    else
    {
        // out
        DEFERRED_UNIQUE_CALL( 1.0, DELEGATE(this, deletevcallnow), 0);
    }

    return false; // always false
}

bool gui_conversation_header_c::av_call( RID, GUIPARAM p )
{
    if ( contact )
    {
        why_this_subget_e why;
        contact_c *cdef = contact->subget_smart(why);
        contact_c *c_call = nullptr;

        contact->subiterate( [&]( contact_c *c ) {
            active_protocol_c *ap = prf().ap( c->getkey().protoid );
            if ( ap && 0 != ( PF_AUDIO_CALLS & ap->get_features() ) )
            {
                if (!p || 0 != (PF_VIDEO_CALLS & ap->get_features()))
                {
                    if (c_call == nullptr || (cdef == c && c->get_state() == CS_ONLINE))
                        c_call = c;
                    if (c->get_state() == CS_ONLINE && c_call != c && c_call->get_state() != CS_ONLINE)
                        c_call = c;
                }
            }
        } );

        if ( c_call )
        {
            c_call->b_call( RID(), p );
            if (acall_button) acall_button->enable(false);
            if (vcall_button) vcall_button->enable(false);
        }

    }
    return true;
}

bool gui_conversation_header_c::apply_edit( RID r, GUIPARAM p )
{
    //text_adapt_user_input(hstuff().curedit);
    if ( flags.is( F_EDITNAME ) )
    {
        flags.clear( F_EDITNAME );
        if ( contact->getkey().is_self )
        {
            if ( !curedit.is_empty() )
            {
                if ( prf().username( curedit ) )
                    gmsg<ISOGM_CHANGED_SETTINGS>( 0, PP_USERNAME, curedit ).send();
            }
            update_text();

        }
        else if ( contact->getkey().is_conference() )
        {
            if ( contact->get_conference_permissions() & CP_CHANGE_NAME )
                if ( active_protocol_c *ap = prf().ap( contact->getkey().protoid ) )
                    ap->rename_conference( contact->getkey().gidcid(), curedit );

        } else if ( contact->get_customname() != curedit )
        {
            contact->set_customname( curedit );
            prf().dirtycontact( contact->getkey() );
            flags.set( F_SKIPUPDATE );
            gmsg<ISOGM_V_UPDATE_CONTACT>( contact, true ).send();
            flags.clear( F_SKIPUPDATE );
            update_text();
        }

    }
    if ( flags.is( F_EDITSTATUS ) )
    {
        flags.clear( F_EDITSTATUS );
        if ( contact->getkey().is_self )
        {
            if ( prf().userstatus( curedit ) )
                gmsg<ISOGM_CHANGED_SETTINGS>( 0, PP_USERSTATUSMSG, curedit ).send();
        }
        update_text();
    }
    g_app->update_buttons_head();
    return true;
}

bool gui_conversation_header_c::cancel_edit( RID, GUIPARAM )
{
    flags.clear( F_EDITNAME | F_EDITSTATUS );
    g_app->update_buttons_head();
    return true;
}

bool gui_conversation_header_c::b_noti_switch( RID, GUIPARAM p )
{
    contact->set_imnb( p ? IMB_DONT_NOTIFY : IMB_DEFAULT );
    if (conference_s *c = contact->find_conference())
        c->change_flag( conference_s::F_SUPPRESS_NOTIFICATIONS, p != nullptr );
    return true;
}


bool gui_conversation_header_c::update_buttons( RID r, GUIPARAM p )
{
    ASSERT( CIR_CONVERSATION_HEAD == role );

    MEMT( MEMT_CONVERSATION );

    if ( p )
    {
        // just update pos of edit buttons
        bool redr = false;
        bool noedt = !flags.is( F_EDITNAME | F_EDITSTATUS );
        for ( rbtn::ebutton_s &b : updr )
            if ( b.updated )
            {
                MODIFY bbm( b.brid );
                redr |= bbm.pos() != b.p;
                bbm.pos( b.p ).setminsize( b.brid ).visible( noedt );
                b.dirty = false;
            }

        if ( redr )
            getengine().redraw();

        if ( noedt && editor )
        {
        }
        else
            return true;
    }

    if ( getrid() == r )
    {
        for ( rbtn::ebutton_s &b : updr )
            if ( b.brid )
            {
                b.updated = false;
                b.dirty = true;
                MODIFY( b.brid ).visible( false );
            }
    }

    if ( !updr[ rbtn::EB_NAME ].brid )
    {
        gui_button_c &b = MAKE_CHILD<gui_button_c>( getrid() );
        b.set_face_getter( BUTTON_FACE_PRELOADED( editb ) );
        b.set_handler( DELEGATE( this, edit0 ), nullptr );
        updr[ rbtn::EB_NAME ].brid = b.getrid();
    }

    if ( !updr[ rbtn::EB_STATUS ].brid )
    {
        gui_button_c &b = MAKE_CHILD<gui_button_c>( getrid() );
        b.set_face_getter( BUTTON_FACE_PRELOADED( editb ) );
        b.set_handler( DELEGATE( this, edit1 ), nullptr );
        updr[ rbtn::EB_STATUS ].brid = b.getrid();
    }

    ts::safe_ptr<leech_edit> le;

    bool vcall_button_was = false;

    if ( flags.is( F_EDITNAME | F_EDITSTATUS ) )
    {
        ts::str_c eval;
        if ( contact->getkey().is_self )
        {
            if ( flags.is( F_EDITNAME ) )
                eval = prf().username();
            else
                eval = prf().userstatus();
        }
        else
        {
            eval = contact->get_customname();
            if ( eval.is_empty() )
                eval = contact->get_name();
        }
        text_prepare_for_edit( eval );
        if ( editor )
        {
        } else
        {
            curedit.clear();

            getengine().trunc_children( 2 );
            for ( rbtn::ebutton_s &b : updr )
                b.dirty = true;

            gui_textfield_c &tf = ( MAKE_CHILD<gui_textfield_c>( getrid(), from_utf8( eval ), MAX_PATH_LENGTH, 0, false ) << DELEGATE( this, _edt ) );
            editor = tf.getrid();
            le = TSNEW( leech_edit, last_head_text_pos.x );
            tf.leech( le );
            gui->set_focus( editor );
            tf.end();
            tf.register_kbd_callback( DELEGATE( this, cancel_edit ), ts::SSK_ESC, false );
            tf.register_kbd_callback( DELEGATE( this, apply_edit ), ts::SSK_ENTER, false );
            tf.register_kbd_callback( DELEGATE( this, apply_edit ), ts::SSK_PADENTER, false );

            gui_button_c &bok = MAKE_CHILD<gui_button_c>( getrid() );
            bok.set_face_getter( BUTTON_FACE_PRELOADED( confirmb ) );
            bok.set_handler( DELEGATE( this, apply_edit ), nullptr );
            bok.leech( TSNEW( leech_at_right, &tf, 5 ) );
            bconfirm = bok.getrid();

            gui_button_c &bc = MAKE_CHILD<gui_button_c>( getrid() );
            bc.set_face_getter( BUTTON_FACE_PRELOADED( cancelb ) );
            bc.set_handler( DELEGATE( this, cancel_edit ), nullptr );
            bc.leech( TSNEW( leech_at_right, &bok, 5 ) );
            bcancel = bc.getrid();

        }

    }
    else
    {
        if (vcall_button)
            vcall_button_was = true;

        getengine().trunc_children( 2 );
        editor = RID();
        bconfirm = RID();
        bcancel = RID();
    }

    if ( acall_button )
        TSDEL( acall_button );

    if (vcall_button)
        TSDEL(vcall_button), vcall_button_was = true;

    if ( b1 )
        TSDEL( b1 );

    if ( b2 )
        TSDEL( b2 );

    if ( b3 )
        TSDEL( b3 );

    if ( contact && !contact->getkey().is_self )
    {
        if ( contact->getkey().is_conference() )
        {
            if ( contact->get_state() == CS_ONLINE )
            {
                gui_button_c &b_noti_off = MAKE_CHILD<gui_button_c>( getrid() );
                b1 = &b_noti_off;

                b_noti_off.set_face_getter( BUTTON_FACE( noti_on ), BUTTON_FACE( noti_off ), contact->get_imnb() != IMB_DONT_NOTIFY );
                b_noti_off.set_handler( DELEGATE( this, b_noti_switch ), nullptr );
                ts::ivec2 minsz = b_noti_off.get_min_size();
                b_noti_off.leech( TSNEW( leech_dock_right_center_s, minsz.x, minsz.y, 2, -1, 0, 1 ) );
                MODIFY( b_noti_off ).visible( true );

                if ( contact->flag_is_av )
                {
                    if (av_contact_s *avcp = g_app->avcontacts().find_inprogress_any( contact ))
                    {
                        gui_button_c &b_mute_speaker = MAKE_CHILD<gui_button_c>( getrid() );
                        b2 = &b_mute_speaker;
                        b_mute_speaker.set_face_getter( BUTTON_FACE( mute_speaker ), BUTTON_FACE( unmute_speaker ), avcp->is_speaker_off() );

                        b_mute_speaker.set_handler( DELEGATE( avcp, b_speaker_switch ), nullptr );
                        minsz = b_mute_speaker.get_min_size();
                        b_mute_speaker.leech( TSNEW( leech_at_left_s, &b_noti_off, 5 ) );
                        MODIFY( b_mute_speaker ).visible( true );



                        gui_button_c &b_mute_mic = MAKE_CHILD<gui_button_c>( getrid() );
                        b3 = &b_mute_mic;

                        b_mute_mic.set_face_getter( BUTTON_FACE( mute_mic ), BUTTON_FACE( unmute_mic ), avcp->is_mic_off() );
                        b_mute_mic.set_handler( DELEGATE( avcp, b_mic_switch ), nullptr );

                        //minsz = b_mute_mic.get_min_size();
                        b_mute_mic.leech( TSNEW( leech_at_left_s, &b_mute_speaker, 5 ) );
                        MODIFY( b_mute_mic ).visible( true );
                    }
                }
            }

        } else
        {
            int features = 0;
            int features_online = 0;
            bool now_disabled = false;
            contact->subiterate( [&]( contact_c *c ) {
                if ( active_protocol_c *ap = prf().ap( c->getkey().protoid ) )
                {
                    int f = ap->get_features();
                    features |= f;
                    if ( c->get_state() == CS_ONLINE ) features_online |= f;
                    if ( c->flag_is_av || c->flag_is_ringtone || c->flag_is_calltone ) now_disabled = true;
                }
            } );

            gui_button_c &b_call = MAKE_CHILD<gui_button_c>( getrid() );
            acall_button = &b_call;
            b_call.set_face_getter( BUTTON_FACE_PRELOADED( callb ) );
            b_call.tooltip( TOOLTIP( TTT( "Call", 140 ) ) );
            b_call.set_handler( DELEGATE( this, av_call ), nullptr );

            ts::ivec2 minsz = b_call.get_min_size();
            b_call.leech(TSNEW(leech_dock_right_center_s, minsz.x, minsz.y, 2, -1, 0, 1));
            b_call.leech(TSNEW(leech_inout_handler_s, DELEGATE(this, avbinout)));

            MODIFY( b_call ).zindex(0.1f).visible( true );

            if ( now_disabled )
            {
                b_call.disable();
                b_call.tooltip( GET_TOOLTIP() );
            }
            else if ( 0 == ( features & PF_AUDIO_CALLS ) )
            {
                b_call.disable();
                b_call.tooltip( TOOLTIP( TTT( "Call not supported", 141 ) ) );
            }
            else if ( 0 == ( features_online & PF_AUDIO_CALLS ) )
            {
                b_call.disable();
                b_call.tooltip( TOOLTIP( TTT( "Contact offline", 143 ) ) );
            }

            if (vcall_button_was)
                avbinout(RID(), as_param(2));

        }
    }

    if (le)
        le->sq_evt( SQ_PARENT_RECT_CHANGED, editor, ts::make_dummy<evt_data_s>( true ) );
    g_app->update_buttons_msg();

    return true;
}










MAKE_CHILD<gui_contact_separator_c>::~MAKE_CHILD()
{
    ASSERT( parent );

    if ( cc )
        get().set_prots_from_contact(cc);
    else
        get().set_text( text );

    MODIFY( get() ).visible( is_visible );

}

gui_contact_separator_c::gui_contact_separator_c( MAKE_CHILD<gui_contact_separator_c> &data ):gui_clist_base_c( data, CIR_SEPARATOR )
{

}
gui_contact_separator_c::~gui_contact_separator_c()
{

}

void gui_contact_separator_c::set_prots_from_contact( const contact_root_c *cc )
{
    prots.clear();

    if ( cc->getkey().is_conference() )
    {
        if ( cc->get_state() == CS_ONLINE )
            prots.add( cc->getkey().protoid );

    } else
    {
        cc->subiterate( [this]( const contact_c *c ) {
            ts::tbuf_t<ts::auint>::default_comparator dc;
            prots.insert_sorted_uniq( c->getkey().protoid, dc );
        } );
    }

    update_text();
}

void gui_contact_separator_c::update_text()
{
    MEMT( MEMT_CSEPARATOR );

    ts::wstr_c t;

    if ( prots.count() == 0 )
    {
        t = TTT("Inactive items",500);
    } else
    {
        for ( int id : prots )
            if ( active_protocol_c *ap = prf().ap( id ) )
            {
                if ( !t.is_empty() ) t.append( CONSTWSTR( " + " ) );
                t.append( ts::from_utf8( ap->get_name() ) );
            }
    }

    set_text( t );
    set_vcenter();
    textrect.set_options( ts::TO_VCENTER );
}

bool gui_contact_separator_c::is_prots_same_as_contact( const contact_root_c *cc ) const
{
    if ( cc->getkey().is_conference() && cc->get_state() == CS_OFFLINE )
        return prots.count() == 0;

    ts::tmp_tbuf_t<ts::uint16> tprots;

    if ( cc->getkey().is_conference() )
    {
        tprots.add( cc->getkey().protoid );
    } else
    {
        cc->subiterate( [&]( const contact_c *c ) {
            ts::tbuf_t<ts::auint>::default_comparator dc;
            tprots.insert_sorted_uniq( c->getkey().protoid, dc );
        } );
    }

    return prots.count() == tprots.count() && ts::blk_equal( prots.begin(), tprots.begin(), prots.count() );

}

/*virtual*/ void gui_contact_separator_c::created()
{
    set_theme_rect( CONSTASTR( "cseparator" ), false );
    defaultthrdraw = DTHRO_BORDER | DTHRO_CENTER;
    set_autoheight();
    gui_control_c::created();

    gui_button_c &collapser = MAKE_CHILD<gui_button_c>( getrid() );
    collapser.set_face_getter( BUTTON_FACE( gexpanded ), BUTTON_FACE( gcollapsed ), flags.is( F_COLLAPSED ) );
    collapser.set_handler( DELEGATE( this, on_collapse_or_expand ), nullptr );
    ts::ivec2 s = collapser.get_min_size();
    collapser.leech( TSNEW( leech_dock_left_center_s, s.x, s.y, 0, 0, 0, 1 ) );
    MODIFY( collapser ).visible( true );

    textrect.set_margins( ts::ivec2( s.x, 0 ) );
}

bool gui_contact_separator_c::on_collapse_or_expand( RID, GUIPARAM p )
{
    flags.init( F_COLLAPSED, p == nullptr );
    getengine().redraw();
    gui_contactlist_c & cl = HOLD( getparent() ).as<gui_contactlist_c>();
    cl.fix_c_visibility();
    cl.scroll_to_child( &getengine(), ST_ANY_POS );

    return true;
}

/*virtual*/ int gui_contact_separator_c::proto_sort_factor() const
{
    return static_cast<int>( prf().protogroupsort( prots.data16(), prots.count() ) );
}

void gui_contact_separator_c::moveup( const ts::str_c& )
{
    prf().protogroupsort_up( prots.data16(), prots.count(), false );
    contacts().resort_list();
    //gui_contactlist_c & cl = HOLD( getparent() ).as<gui_contactlist_c>();
    //cl.scroll_to_child( &getengine(), ST_ANY_POS );
}

void gui_contact_separator_c::movedn( const ts::str_c& )
{
    prf().protogroupsort_dn( prots.data16(), prots.count(), false );
    contacts().resort_list();
}

/*virtual*/ bool gui_contact_separator_c::sq_evt( system_query_e qp, RID rid, evt_data_s &data )
{
    MEMT( MEMT_CSEPARATOR );

    if ( rid != getrid() )
    {
        // from submenu
        if ( popupmenu && popupmenu->getrid() == rid )
        {
            if ( SQ_POPUP_MENU_DIE == qp )
                MODIFY( *this ).highlight( false );
        }
        return false;
    }

    switch ( qp )
    {
    case SQ_MOUSE_RDOWN:
        clicka = SQ_NOP;
        if (popupmenu.expired())
            clicka = SQ_MOUSE_RUP;
        return true;
    case SQ_MOUSE_RUP:
        if (clicka == qp)
        {
            bool allow_move_up = prf().protogroupsort_up( prots.data16(), prots.count(), true );
            bool allow_move_down = prf().protogroupsort_dn( prots.data16(), prots.count(), true );

            if (allow_move_up || allow_move_down)
            {
                menu_c m;
                if (allow_move_up) m.add( loc_text( loc_moveup ), 0, DELEGATE( this, moveup ) );
                if (allow_move_down) m.add( loc_text( loc_movedn ), 0, DELEGATE( this, movedn ) );
                gui_popup_menu_c::show( menu_anchor_s( true ), m );
            }

        }
        break;
    case SQ_DRAW:

        //if ( !prf().is_loaded() )
        //    return gui_control_c::sq_evt( qp, rid, data );

        //{
        //    gui_control_c::sq_evt( qp, rid, data );

        //    ts::irect ca = get_client_area();
        //    draw_data_s &dd = m_engine->begin_draw();
        //    dd.size = ca.size();
        //    if ( dd.size >> 0 )
        //    {
        //        text_draw_params_s tdp;
        //        dd.offset += ca.lt;
        //        ts::flags32_s f; f.setup( ts::TO_VCENTER | ts::TO_LINE_END_ELLIPSIS );
        //        tdp.textoptions = &f; //-V506
        //        tdp.forecolor = nullptr;
        //        draw( dd, tdp );
        //    }
        //    m_engine->end_draw();

        //}
        super::sq_evt( qp, rid, data );
        return true;
    }

    return super::sq_evt( qp, rid, data );
}





MAKE_CHILD<gui_contactlist_c>::~MAKE_CHILD()
{
    ASSERT(parent);
    MODIFY(get()).show();
}

gui_contactlist_c::~gui_contactlist_c()
{
    if (gui)
    {
        gui->delete_event(DELEGATE(this, on_filter_deactivate));
        gui->delete_event(DELEGATE(this, refresh_list));
    }
}

void gui_contactlist_c::array_mode( ts::array_inplace_t<contact_key_s, 2> & arr_ )
{
    ASSERT(role != CLR_MAIN_LIST);
    arr = &arr_;
    refresh_array();
}

void gui_contactlist_c::clearlist()
{
    getengine().trunc_children( skipctl );
}

void gui_contactlist_c::fix_c_visibility()
{
    bool vis = true;
    for ( ts::aint v = getengine().get_next_child_index( skipctl ); v >= 0; v = getengine().get_next_child_index( v + 1 ) )
    {
        gui_clist_base_c *clb = ts::ptr_cast<gui_clist_base_c *>( &getengine().get_child( v )->getrect() );
        if ( gui_contact_separator_c *sep = clb->as_separator() )
            vis = !sep->is_collapsed();
        else if ( gui_contact_item_c *itm = clb->as_item() )
            itm->vis_group( vis );
    }
}

void gui_contactlist_c::fix_sep_visibility()
{
    gui_contact_separator_c *checksep = nullptr;
    bool vis_filter = false;

    for( ts::aint v = getengine().get_next_child_index(skipctl); v >= 0; v = getengine().get_next_child_index(v+1) )
    {
        gui_clist_base_c *clb = ts::ptr_cast<gui_clist_base_c *>( &getengine().get_child( v )->getrect() );
        if ( checksep == nullptr )
        {
            checksep = clb->as_separator();
            vis_filter = false;
            continue;
        }

        if (gui_contact_separator_c *nextsep = clb->as_separator())
        {
            MODIFY( *checksep ).visible( vis_filter );
            //checksep->no_collapsed();
            checksep = nextsep;
            vis_filter = false;

        } else if ( gui_contact_item_c *itm = clb->as_item() )
        {
            vis_filter |= itm->is_vis_filter();

            if ( !itm->getprops().is_visible() )
                continue;

            MODIFY( *checksep ).visible( true );
            checksep->update_text();
            checksep = nullptr;
        }
    }
    if (checksep)
    {
        MODIFY( *checksep ).visible( vis_filter );
        //checksep->no_collapsed();
    }
}

void gui_contactlist_c::refresh_array()
{
    ASSERT(role != CLR_MAIN_LIST && arr != nullptr);

    ts::aint count = getengine().children_count();
    int index = 0;
    for ( ts::aint i = skipctl; i < count; ++i, ++index)
    {
        rectengine_c *ch = getengine().get_child(i);
        if (ch)
        {
            gui_contact_item_c *ci = ts::ptr_cast<gui_contact_item_c *>(&ch->getrect());

            loopcheg:
            if (index >= arr->size())
            {
                getengine().trunc_children(index);
                return;
            }

            const contact_key_s &ck = arr->get(index);
            if (ci->getcontact().getkey() == ck)
                continue;

            contact_root_c * c = contacts().rfind(ck);
            if (!c)
            {
                arr->remove_slow(index);
                goto loopcheg;
            }
            ci->setcontact( c );
            MODIFY(*ci).active(false);
        }
    }

    for (;index < arr->size();)
    {
        const contact_key_s &ck = arr->get(index);
        contact_c * c = contacts().find(ck);
        if (!c)
        {
            arr->remove_slow(index);
            continue;;
        }
        MAKE_CHILD<gui_contact_item_c>(getrid(), c->get_historian()) << CIR_METACREATE;
        ++index;
    }
}

/*virtual*/ ts::ivec2 gui_contactlist_c::get_min_size() const
{
    ts::ivec2 sz(200);
    sz.y += skip_top_pixels + skip_bottom_pixels;
    return sz;
}

/*virtual*/ void gui_contactlist_c::created()
{
    set_theme_rect(CONSTASTR("contacts"), false);
    super::created();

    if (role == CLR_MAIN_LIST)
    {
        recreate_ctls();
        //contacts().update_meta();
    } else if (role == CLR_NEW_METACONTACT_LIST)
    {
        skip_top_pixels = 0;
        skip_bottom_pixels = 0;
        skipctl = 0;
    }
}

void gui_contactlist_c::recreate_ctls(bool focus_filter)
{
    MEMT( MEMT_CONTACTLIST );

    if (filter)
    {
        TSDEL(filter);
        DEFERRED_UNIQUE_CALL( 0, DELEGATE(this, on_filter_deactivate), nullptr );
    }
    if (addcbtn) TSDEL(addcbtn);
    if (addgbtn) TSDEL(addgbtn);
    if (self) TSDEL(self);

    if (button_desc_s *baddc = gui->theme().get_button(CONSTASTR("addcontact")))
    {
        struct handlers
        {
            static bool summon_addcontacts(RID, GUIPARAM)
            {
                if (prf().is_loaded())
                    SUMMON_DIALOG<dialog_addcontact_c>(UD_ADDCONTACT);
                else
                    dialog_msgbox_c::mb_error( loc_text(loc_please_create_profile) ).summon(true);
                return true;
            }
            static bool summon_addconference(RID, GUIPARAM)
            {
                if (prf().is_loaded())
                    SUMMON_DIALOG<dialog_addconference_c>(UD_ADDGROUP);
                else
                    dialog_msgbox_c::mb_error( loc_text(loc_please_create_profile) ).summon(true);
                return true;
            }
        };

        button_desc_s *baddg = gui->theme().get_button(CONSTASTR("addconf"));
        int nbuttons = baddg ? 2 : 1;

        flags.set(F_NO_LEECH_CHILDREN);

        addcbtn = MAKE_CHILD<gui_button_c>(getrid());
        addcbtn->tooltip(TOOLTIP(TTT("Add contact",64)));
        addcbtn->set_face_getter(BUTTON_FACE(addcontact));
        addcbtn->set_handler(handlers::summon_addcontacts, nullptr);
        addcbtn->leech(TSNEW(leech_dock_bottom_center_s, baddc->size.x, baddc->size.y, -10, 10, 0, nbuttons));
        MODIFY(*addcbtn).zindex(1.0f).show();
        getengine().child_move_to(0, &addcbtn->getengine());

        if ( !prf().is_any_active_ap() )
        {
            addcbtn->tooltip(TOOLTIP(loc_text(loc_nonetwork)));
            addcbtn->disable();
        }

        if (baddg)
        {
            addgbtn = MAKE_CHILD<gui_button_c>(getrid());
            addgbtn->tooltip(TOOLTIP(TTT("Add conference",243)));
            addgbtn->set_face_getter(BUTTON_FACE(addconf));
            addgbtn->set_handler(handlers::summon_addconference, nullptr);
            addgbtn->leech(TSNEW(leech_dock_bottom_center_s, baddg->size.x, baddg->size.y, -10, 10, 1, 2));
            MODIFY(*addgbtn).zindex(1.0f).show();
            getengine().child_move_to(1, &addgbtn->getengine());

            if (!prf().is_any_active_ap(PF_CONFERENCE))
            {
                if (prf().is_loaded())
                    addgbtn->tooltip(TOOLTIP(TTT("No any active networks with conference support",247)));
                else
                    addgbtn->tooltip(nullptr);
                addgbtn->disable();
            }
        }

        self = MAKE_CHILD<gui_contact_item_c>(getrid(), &contacts().get_self()) << CIR_ME;
        self->leech(TSNEW(leech_dock_top_s, GET_THEME_VALUE(mecontactheight)));
        MODIFY(*self).zindex(1.0f).show();
        getengine().child_move_to(nbuttons, &self->getengine());

        flags.clear(F_NO_LEECH_CHILDREN);

        int other_ctls = 1;
        if (prf().is_loaded() && prf_options().is(UIOPT_TAGFILETR_BAR| UIOPT_SHOW_SEARCH_BAR))
        {
            other_ctls = 2;
            filter = MAKE_CHILD<gui_filterbar_c>(getrid());
            getengine().child_move_to(nbuttons + 1, &filter->getengine());
            DEBUGCODE(skip_top_pixels = 0);
            update_filter_pos();
            MODIFY(*filter).zindex(1.0f).show();

            ASSERT(skip_top_pixels > 0); // it will be calculated via update_filter_pos

            if (focus_filter)
                filter->focus_edit();
        } else
        {
            skip_top_pixels = gui->theme().conf().get_int(CONSTASTR("cltop"), 70);
        }

        getengine().z_resort_children();

        skipctl = other_ctls + nbuttons;
        //skip_top_pixels = gui->theme().conf().get_int(CONSTASTR("cltop"), 70) + filter->get_height_by_width( get_client_area().width() );
        skip_bottom_pixels = gui->theme().conf().get_int(CONSTASTR("clbottom"), 70);
        return;
    }
    skipctl = 0;
    gui->repos_children( this );
}

bool gui_contactlist_c::i_leeched( guirect_c &to )
{
    if (flags.is(F_NO_LEECH_CHILDREN))
        return false;

    return true;
}

bool gui_contactlist_c::on_filter_deactivate(RID, GUIPARAM)
{
    if (filter && !filter->is_all())
        return true;

    ts::safe_ptr<rectengine_c> active = g_app->active_contact_item ? &g_app->active_contact_item->getengine() : nullptr;

    contacts().iterate_root_contacts([](contact_root_c *c)->bool{

        if ( c->getkey().is_self )
            return true;

        bool redraw = false;
        if (c->flag_full_search_result)
        {
            c->flag_full_search_result = false;
            redraw = true;
        }
        if (c->gui_item)
        {
            c->gui_item->vis_filter(true);
            if (redraw)
            {
                c->gui_item->update_text();
                c->redraw();
            }
        }
        return true;
    });

    fix_sep_visibility();

    if (active)
        scroll_to_child(active, ST_ANY_POS);
    else if (!flags.is(F_KEEP_SCROLL_POS))
        scroll_to_begin();

    gmsg<ISOGM_REFRESH_SEARCH_RESULT>().send();

    return true;
}

void gui_contactlist_c::update_filter_pos()
{
    ASSERT(!filter.expired() && !self.expired());
    ts::irect cla = get_client_area();
    int claw = cla.width();
    int fh = claw ? filter->get_height_by_width( claw ) : 20;
    int selfh = gui->theme().conf().get_int(CONSTASTR("cltop"), 70);
    MODIFY(*filter.get()).pos( cla.lt.x, cla.lt.y + selfh ).size( claw, fh );

    int otp = skip_top_pixels;
    skip_top_pixels = selfh + fh;
    if (skip_top_pixels != otp)
        gui->repos_children(this);
}

bool gui_contactlist_c::filter_proc(system_query_e qp, evt_data_s &data)
{
    if (qp == SQ_PARENT_RECT_CHANGED)
    {
        update_filter_pos();
        return false;
    }

    return false;
}

ts::uint32 gui_contactlist_c::gm_handler(gmsg<ISOGM_CALL_STOPED> &p)
{
    p.subcontact->redraw();
    return 0;
}

ts::uint32 gui_contactlist_c::gm_handler(gmsg<ISOGM_TYPING> &p)
{
    if (prf_options().is(UIOPT_SHOW_TYPING_CONTACT))
        if (contact_c *c = contacts().find(p.contact))
            if (contact_root_c *historian = c->get_historian())
                if (historian->gui_item)
                    historian->gui_item->typing();

    return 0;
}

ts::uint32 gui_contactlist_c::gm_handler(gmsg<ISOGM_PROFILE_TABLE_SL> &p)
{
    if (!p.saved)
        return 0;

    if (p.tabi == pt_active_protocol)
        g_app->recreate_ctls(true, false);
    return 0;
}

ts::uint32 gui_contactlist_c::gm_handler(gmsg<ISOGM_PROTO_LOADED>&)
{
    g_app->recreate_ctls(true, false);
    contacts().update_roots();

    ts::aint count = getengine().children_count();
    for ( ts::aint i = skipctl; i < count; ++i )
    {
        if ( rectengine_c *ch = getengine().get_child( i ) )
        {
            guirect_c &cirect = ch->getrect();
            if ( !cirect.getprops().is_visible() ) continue;

            if ( gui_contact_separator_c *ci = ts::ptr_cast<gui_clist_base_c *>( &cirect )->as_separator() )
                ci->update_text();
        }
    }

    return 0;
}

ts::uint32 gui_contactlist_c::gm_handler(gmsg<ISOGM_CHANGED_SETTINGS>&ch)
{
    if (ch.pass > 0 && self)
    {
        switch (ch.sp)
        {
            case PP_USERNAME:
            case PP_USERSTATUSMSG:
                if (0 != ch.protoid)
                    break;
            case PP_NETWORKNAME:
                self->update_text();
                break;
        }
    } else if ( ch.pass == 0 && ch.protoid == 0 && self )
    {
        switch (ch.sp)
        {
            case PP_USERNAME:
            case PP_USERSTATUSMSG:
                self->update_text();
                break;
        }
    }

    if (ch.pass == 0 && self)
        if (PP_ONLINESTATUS == ch.sp)
            self->getengine().redraw();

    if (ch.pass == 0)
        if ( PP_PROFILEOPTIONS == ch.sp )
        {
            if ( ch.bits & ( UIOPT_TAGFILETR_BAR | UIOPT_SHOW_SEARCH_BAR ) )
                recreate_ctls( true );

            if ( ch.bits & ( CLOPT_GROUP_CONTACTS_PROTO ) )
            {
                clearlist();

                DEFERRED_EXECUTION_BLOCK_BEGIN( 0 )
                    contacts().resort_list();
                    contacts().update_roots();
                DEFERRED_EXECUTION_BLOCK_END( 0 )

            }
        }

    if ( ch.pass == 0 && PP_NETWORKNAME == ch.sp )
    {
        contacts().iterate_proto_contacts( [&]( contact_c *c )->bool {

            if ( c->is_system_user && c->getkey().protoid == (unsigned)ch.protoid )
            {
                c->set_name( ch.s );
                if ( c->get_historian()->gui_item )
                    c->get_historian()->gui_item->update_text();
            }

            return true;
        } );
    }


    return 0;
}

ts::uint32 gui_contactlist_c::gm_handler(gmsg<GM_UI_EVENT> &ue)
{
    if (UE_THEMECHANGED == ue.evt)
    {
        recreate_ctls();

        ts::aint count = getengine().children_count();
        for ( ts::aint i = skipctl; i < count; ++i)
        {
            if (rectengine_c *ch = getengine().get_child(i))
            {
                guirect_c &cirect = ch->getrect();
                if (!cirect.getprops().is_visible()) continue;

                bool a = HOLD(cirect)().getprops().is_active();
                MODIFY(cirect).active(!a);
                MODIFY(cirect).active(a);

                if (gui_contact_item_c *ci = ts::ptr_cast<gui_clist_base_c *>(&cirect)->as_item())
                    ci->update_text();
            }
        }

    }
    return 0;
}


ts::uint32 gui_contactlist_c::gm_handler(gmsg<GM_DRAGNDROP> &dnda)
{
    if (dnda.a == DNDA_CLEAN)
    {
        if (dndtarget) dndtarget->target(false);
        dndtarget = nullptr;
        return 0;
    }
    gui_contact_item_c *ciproc = dynamic_cast<gui_contact_item_c *>(gui->dragndrop_underproc());
    if (!ciproc) return 0;
    if (dnda.a == DNDA_DROP)
    {
        if (dndtarget) dndtarget->on_drop( ciproc );
        dndtarget = nullptr;
    }

    ts::irect rect = gui->dragndrop_objrect();
    int area = rect.area() / 3;
    rectengine_c *yo = nullptr;
    ts::aint count = getengine().children_count();
    for ( ts::aint i = skipctl; i < count; ++i)
    {
        rectengine_c *ch = getengine().get_child(i);
        if (ch)
        {
            const guirect_c &cirect = ch->getrect();
            if (!cirect.getprops().is_visible() || cirect.getprops().out_of_bound()) continue;
            if (&cirect == ciproc) continue;

            const gui_contact_item_c *ci = ts::ptr_cast<const gui_clist_base_c *>( &cirect )->as_item();
            if (!ci) continue;

            int carea = cirect.getprops().screenrect().intersect_area( rect );
            if (carea > area && ci->allow_drop() )
            {
                area = carea;
                yo = ch;
            }
        }
    }
    if (yo)
    {
        gui_contact_item_c *ci = ts::ptr_cast<gui_contact_item_c *>(&yo->getrect());
        if (dndtarget != ci)
        {
            if (dndtarget)
                dndtarget->target(false);
            ci->target(true);
            dndtarget = ci;
        }
    } else if (dndtarget)
    {
        dndtarget->target(false);
        dndtarget = nullptr;
    }

    return yo ? GMRBIT_ACCEPTED : 0;
}

bool gui_contactlist_c::refresh_list(RID, GUIPARAM p)
{
    MEMT( MEMT_CONTACTLIST );

    if (filter)
        filter->refresh_list( p != nullptr );

    return true;
}

ts::uint32 gui_contactlist_c::gm_handler( gmsg<ISOGM_V_UPDATE_CONTACT> & c )
{
    MEMT( MEMT_CONTACT_ITEM );

    if ( contact_root_c *h = c.contact->get_historian() )
        if (h->getkey().is_self)
        {
            if (self && self->contacted())
                if (ASSERT(c.contact == &self->getcontact() || c.contact->getmeta() == &self->getcontact()))
                    self->update_text();
            return 0;
        }

    auto find_sep = [this]( contact_root_c *cc ) ->ts::aint
    {
        for ( ts::aint i = skipctl, cnt = getengine().children_count(); i < cnt; ++i )
        {
            if (rectengine_c *ch = getengine().get_child( i ))
            {
                gui_clist_base_c *clb = ts::ptr_cast<gui_clist_base_c *>( &ch->getrect() );
                if ( gui_contact_separator_c *sep = clb->as_separator() )
                {
                    if ( sep->is_prots_same_as_contact( cc ) )
                    {
                        return i + 1;
                    }
                }
            }
        }
        return -1;

    };

    bool proto_sep = prf_options().is( CLOPT_GROUP_CONTACTS_PROTO );

    ts::aint count = getengine().children_count();
    for( ts::aint i=skipctl;i<count;++i)
    {
        if ( rectengine_c *ch = getengine().get_child( i ) )
        {
            gui_clist_base_c *clb = ts::ptr_cast<gui_clist_base_c *>(&ch->getrect());
            if (gui_contact_item_c *ci = clb->as_item())
            {
                if (!ci->contacted())
                    continue;
                bool same = c.contact == &ci->getcontact();
                if ( same && c.contact->get_state() == CS_ROTTEN )
                {
                    TSDEL( ch );
                    getengine().redraw();
                    gui->dragndrop_update( nullptr );
                    return 0;
                }
                if ( same || ci->getcontact().subpresent( c.contact->getkey() ) )
                {
                    ci->update_text();
                    gui->dragndrop_update( ci );
                    if ( same || !ci->getcontact().getkey().is_conference() )
                    {
                        if ( proto_sep && find_sep( &ci->getcontact() ) < 0 )
                        {
                            MAKE_CHILD<gui_contact_separator_c> sep( getrid(), &ci->getcontact() );
                            getengine().child_move_to(i, &sep.get().getengine(), skipctl );
                        }

                        if ( filter && c.refilter )
                            DEFERRED_UNIQUE_CALL( 0.1, DELEGATE( this, refresh_list ), nullptr );
                        return 0;
                    }
                }
            }
        }
    }

    if (role == CLR_MAIN_LIST)
    {
        ts::aint insindex = -1;

        contact_root_c *cc = c.contact->get_historian();

        if ( proto_sep )
        {
            insindex = find_sep( cc );

            if ( insindex < 0 )
            {
                MAKE_CHILD<gui_contact_separator_c>( getrid(), cc );
            }
        }

        MAKE_CHILD<gui_contact_item_c> mc(getrid(), cc);
        if (filter)
            mc.is_visible = filter->check_one(mc.contact);

        if ( insindex >= 0 )
            getengine().child_move_to( insindex, &mc.get().getengine(), skipctl );
    }

    return 0;
}

ts::uint32 gui_contactlist_c::gm_handler(gmsg<ISOGM_DO_POSTEFFECT> &f)
{
    if (f.bits & application_c::PEF_RECREATE_CTLS_CL)
        recreate_ctls();

    return 0;
}

ts::uint32 gui_contactlist_c::gm_handler(gmsg<GM_HEARTBEAT> &)
{
    MEMT( MEMT_CONTACTLIST );

    if (contacts().sort_tag() != sort_tag && role == CLR_MAIN_LIST && gui->dragndrop_underproc() == nullptr)
    {
        struct sss
        {
            ts::tmp_pointers_t<gui_contact_item_c, 128> sp;
            ts::hashmap_t<size_t, uint64 > sortv;

            size_t ch( gui_clist_base_c *c )
            {
                return calc_hash( &c, sizeof( gui_clist_base_c * ) );
            }

            uint64 &addh( gui_clist_base_c *c )
            {
                return sortv.add( ch(c) );
            }

            bool swap_them( rectengine_c *e1, rectengine_c *e2 )
            {
                if ( e1 == nullptr || e2 == nullptr ) return false;
                if ( e1 == e2 ) return false;
                gui_clist_base_c * ci1 = dynamic_cast<gui_clist_base_c *>( &e1->getrect() );
                if ( ci1 == nullptr ) return false;
                gui_clist_base_c * ci2 = dynamic_cast<gui_clist_base_c *>( &e2->getrect() );
                if ( ci2 == nullptr ) return false;

                if ( ci1->getrole() == CIR_ME || ci2->getrole() == CIR_ME )
                    return false;

                auto *f1 = sortv.find( ch(ci1) );
                if ( !f1 ) return false;
                auto *f2 = sortv.find( ch(ci2) );
                if ( !f2 ) return false;

                return f1->value > f2->value;

                /*
                if ( ci1->getrole() == CIR_LISTITEM && ci2->getrole() == CIR_LISTITEM )
                {
                gui_contact_item_c *i1 = ci1->as_item();
                gui_contact_item_c *i2 = ci2->as_item();
                if ( !prf_options().is( CLOPT_GROUP_CONTACTS_PROTO ) || i1->same_prots(*i2))
                return i1->is_after( *i2 );
                }

                if ( ci1->getrole() == CIR_SEPARATOR && ci2->getrole() == CIR_LISTITEM )
                {
                gui_contact_separator_c *sep = ci1->as_separator();
                gui_contact_item_c *itm = ci2->as_item();
                if ( sep->is_prots_same_as_contact( &itm->getcontact() ) )
                return false;
                }
                if ( ci2->getrole() == CIR_SEPARATOR && ci1->getrole() == CIR_LISTITEM )
                {
                gui_contact_separator_c *sep = ci2->as_separator();
                gui_contact_item_c *itm = ci1->as_item();
                if ( sep->is_prots_same_as_contact( &itm->getcontact() ) )
                return true;
                }


                int sf1 = ci1->proto_sort_factor();
                int sf2 = ci2->proto_sort_factor();

                if ( sf1 == sf2 )
                return ci1 < ci2;
                return sf1 > sf2;
                */
            };

        } ss;

        ts::aint count = getengine().children_count();
        for ( ts::aint i = skipctl; i < count; ++i )
        {
            if ( rectengine_c *e = getengine().get_child( i ) )
            {
                gui_clist_base_c * b = dynamic_cast<gui_clist_base_c *>( &e->getrect() );
                if ( gui_contact_item_c *ci = b->as_item() )
                {
                    bool a = false;
                    for ( ts::aint z = 0, c = ss.sp.size(); z < c; ++z )
                        if ( ss.sp.get( z )->is_after( *ci ) )
                        {
                            a = true;
                            ss.sp.insert( z, ci );
                            break;
                        }
                    if ( !a )
                        ss.sp.add(ci);
                } else if ( gui_contact_separator_c *sep = b->as_separator() )
                    ss.addh( sep ) = static_cast<uint64>( sep->proto_sort_factor() ) << 32;
            }
        }

        bool group = prf_options().is( CLOPT_GROUP_CONTACTS_PROTO );

        for ( ts::aint i = 0, c = ss.sp.size(); i < c; ++i )
        {
            uint64 v = i + 1;

            if ( group )
                v |= ( ( (uint64)ss.sp.get( i )->proto_sort_factor() ) << 32 );

            ss.addh( ss.sp.get( i ) ) = v;
        }


        if (getengine().children_sort( DELEGATE( &ss, swap_them ) ))
        {
            if (g_app->active_contact_item)
                scroll_to_child(&g_app->active_contact_item->getengine(), ST_ANY_POS);

            gui->repos_children(this);
        }
        fix_sep_visibility();
        sort_tag = contacts().sort_tag();
    }
    return 0;
}

/*virtual*/ void gui_contactlist_c::on_manual_scroll(manual_scroll_e ms)
{
    if (MS_BY_MOUSE == ms)
        flags.set(F_KEEP_SCROLL_POS);

    super::on_manual_scroll(ms);
}

/*virtual*/ void gui_contactlist_c::children_repos_info(cri_s &info) const
{
    info.area = get_client_area();
    info.area.lt.y += skip_top_pixels;
    info.area.rb.y -= skip_bottom_pixels;

    info.from = skipctl;
    info.count = getengine().children_count() - skipctl;
}

/*virtual*/ bool gui_contactlist_c::test_under_point( const guirect_c &r, const ts::ivec2& screenpos ) const
{
    if (super::test_under_point(r,screenpos))
    {
        if (const gui_clist_base_c * cb = dynamic_cast<const gui_clist_base_c *>(&r))
            if (const gui_contact_item_c *itm = cb->as_item())
                if (itm->getrole() == CIR_LISTITEM)
                {
                    ts::irect localarea = get_client_area();
                    localarea.lt.y += skip_top_pixels;
                    localarea.rb.y -= skip_bottom_pixels;
                    return localarea.inside( to_local( screenpos ) );
                }

        return true;
    }
    return false;
}


/*virtual*/ bool gui_contactlist_c::sq_evt(system_query_e qp, RID rid, evt_data_s &data)
{
    if (rid != getrid() && ASSERT((getrid() >> rid) || HOLD(rid)().is_root())) // child?
    {
        if (filter && rid == filter->getrid())
            return filter_proc(qp, data);

        return super::sq_evt(qp,rid,data);
    }
    return super::sq_evt(qp, rid, data);
}
