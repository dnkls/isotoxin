#include "isotoxin.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// message box //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

dialog_msgbox_params_s dialog_msgbox_c::mb_info(const ts::wstr_c &text)
{
    return dialog_msgbox_params_s(title_information, text);
}
dialog_msgbox_params_s dialog_msgbox_c::mb_warning(const ts::wstr_c &text)
{
    return dialog_msgbox_params_s(title_warning, text);
}
dialog_msgbox_params_s dialog_msgbox_c::mb_error(const ts::wstr_c &text)
{
    return dialog_msgbox_params_s(title_error, text);
}

RID dialog_msgbox_params_s::summon()
{
    return SUMMON_DIALOG<dialog_msgbox_c>(UD_NOT_UNIQUE, *this);
}


dialog_msgbox_c::dialog_msgbox_c(MAKE_ROOT<dialog_msgbox_c> &data) :gui_isodialog_c(data), m_params(data.prms)
{
    deftitle = m_params.etitle;
    gui->register_kbd_callback( DELEGATE( this, copy_text ), SSK_C, gui_c::casw_ctrl);
    gui->register_kbd_callback( DELEGATE( this, on_enter_press_func ), SSK_ENTER, 0);
    gui->register_kbd_callback( DELEGATE( this, on_enter_press_func ), SSK_PADENTER, 0);
    gui->register_kbd_callback( DELEGATE( this, on_esc_press_func ), SSK_ESC, 0);

    if ( m_params.bcancel_ )
    {
        bcreate_s &bcr = m_buttons.add();
        bcr.tag = 0;
        __super::getbutton(bcr);
        bcr.tag = m_buttons.size()-1;
        if (!m_params.cancel_button_text.is_empty())
            bcr.btext = m_params.cancel_button_text;
    }
    if (m_params.bok_)
    {
        bcreate_s &bcr = m_buttons.add();
        bcr.tag = 1;
        __super::getbutton(bcr);
        bcr.tag = m_buttons.size() - 1;
        if (!m_params.ok_button_text.is_empty())
            bcr.btext = m_params.ok_button_text;
    }
}

dialog_msgbox_c::~dialog_msgbox_c()
{
    if (gui)
    {
        gui->unregister_kbd_callback(DELEGATE(this, copy_text));
        gui->unregister_kbd_callback(DELEGATE(this, on_enter_press_func));
        gui->unregister_kbd_callback(DELEGATE(this, on_esc_press_func));
    }
}

bool dialog_msgbox_c::on_enter_press_func(RID, GUIPARAM)
{
    on_confirm();
    return true;
}

bool dialog_msgbox_c::on_esc_press_func(RID, GUIPARAM)
{
    on_close();
    return true;
}


bool dialog_msgbox_c::copy_text(RID, GUIPARAM)
{
    ts::str_c utf8 = ts::to_utf8(m_params.desc);
    text_remove_tags(utf8);
    ts::set_clipboard_text(ts::from_utf8(utf8));
    return true;
}

/*virtual*/ void dialog_msgbox_c::created()
{
    gui->exclusive_input(getrid());
    set_theme_rect(CONSTASTR("main"), false);
    __super::created();
    tabsel(CONSTASTR("1"));

    WINDOWS_ONLY
    switch (m_params.etitle) //-V719
    {
        case title_error:
            MessageBeep(MB_ICONERROR);
            break;
        case title_information:
            MessageBeep(MB_ICONINFORMATION);
            break;
        case title_warning:
            MessageBeep(MB_ICONWARNING);
            break;
    }
}

void dialog_msgbox_c::getbutton(bcreate_s &bcr)
{
    if (bcr.tag >= m_buttons.size())
        return;
    bcr = m_buttons.get(bcr.tag);
}

/*virtual*/ int dialog_msgbox_c::additions(ts::irect &)
{

    descmaker dm(descs);
    dm << 1;

    dm().label(CONSTWSTR("<p=c>") + m_params.desc).setname(CONSTASTR("label"));

    return 0;
}

/*virtual*/ bool dialog_msgbox_c::sq_evt(system_query_e qp, RID rid, evt_data_s &data)
{
    if (SQ_DRAW == qp && rid == getrid())
    {
        if (RID lbl = find(CONSTASTR("label")))
        {
            ts::ivec2 sz1( getprops().size() - ts::ivec2(30) );
            if (RID b = find(CONSTASTR("dialog_button_0")))
                sz1 = HOLD(b)().getprops().pos();

            ts::irect ca = get_client_area();
            int y0 = ca.lt.y;
            ts::irect viewrect = ts::irect( ca.lt.x + 10, y0 + 2, ca.rb.x - 10, sz1.y - 2 );
            HOLD rlbl(lbl);

            gui_label_c &l = rlbl.as<gui_label_c>();
            l.set_autoheight(false);
            l.set_vcenter();

            MODIFY(lbl).pos( viewrect.lt ).size(viewrect.size());
        }
    }

    if (__super::sq_evt(qp, rid, data)) return true;

    return false;
}

/*virtual*/ void dialog_msgbox_c::on_confirm()
{
    if (m_params.on_ok_h) m_params.on_ok_h(m_params.on_ok_par);
    __super::on_confirm();
}

/*virtual*/ void dialog_msgbox_c::on_close()
{
    if (m_params.on_cancel_h) m_params.on_cancel_h(m_params.on_cancel_par);
    __super::on_close();
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// progress bar /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

dialog_pb_c::dialog_pb_c(MAKE_ROOT<dialog_pb_c> &data) :gui_isodialog_c(data), m_params(data.prms)
{
    if (m_params.bcancel_)
    {
        bcreate_s &bcr = m_buttons.add();
        bcr.tag = 0;
        __super::getbutton(bcr);
        bcr.tag = m_buttons.size() - 1;
        if (!m_params.cancel_button_text.is_empty())
            bcr.btext = m_params.cancel_button_text;
    }
    if (m_params.bok_)
    {
        bcreate_s &bcr = m_buttons.add();
        bcr.tag = 1;
        __super::getbutton(bcr);
        bcr.tag = m_buttons.size() - 1;
        if (!m_params.ok_button_text.is_empty())
            bcr.btext = m_params.ok_button_text;
    }
}

dialog_pb_c::~dialog_pb_c()
{
    if (m_params.pbproc)
        m_params.pbproc->on_close();
}

/*virtual*/ void dialog_pb_c::created()
{
    gui->exclusive_input(getrid());
    set_theme_rect(CONSTASTR("main"), false);
    __super::created();
    tabsel(CONSTASTR("1"));
}

void dialog_pb_c::getbutton(bcreate_s &bcr)
{
    if (bcr.tag >= m_buttons.size())
        return;
    bcr = m_buttons.get(bcr.tag);
}

/*virtual*/ int dialog_pb_c::additions(ts::irect &)
{
    descmaker dm(descs);
    dm << 1;

    if (!m_params.desc.is_empty())
        dm().label(CONSTWSTR("<p=c>") + m_params.desc).setname(CONSTASTR("label"));
    else
        dm().vspace(32);
    dm().hslider(ts::wstr_c(), 0, CONSTWSTR("0/1/0/1"), nullptr).setname(CONSTASTR("bp"));

    return 0;
}

/*virtual*/ void dialog_pb_c::tabselected(ts::uint32 /*mask*/)
{
    if (RID pb = find(CONSTASTR("bp")))
        HOLD(pb).as<gui_hslider_c>().set_pb_mode();

    if (m_params.pbproc)
        m_params.pbproc->on_create(this);

}

void dialog_pb_c::set_level( float v, const ts::wstr_c &txt )
{
    if (RID pb = find(CONSTASTR("bp")))
        HOLD(pb).as<gui_hslider_c>().set_level(v, txt);
}

/*virtual*/ bool dialog_pb_c::sq_evt(system_query_e qp, RID rid, evt_data_s &data)
{
    if (SQ_DRAW == qp && rid == getrid())
    {
    }

    if (__super::sq_evt(qp, rid, data)) return true;

    return false;
}

/*virtual*/ void dialog_pb_c::on_confirm()
{
    if (m_params.on_ok_h) m_params.on_ok_h(m_params.on_ok_par);
    __super::on_confirm();
}

/*virtual*/ void dialog_pb_c::on_close()
{
    if (m_params.on_cancel_h) m_params.on_cancel_h(m_params.on_cancel_par);
    __super::on_close();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// enter text ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


dialog_entertext_c::dialog_entertext_c(MAKE_ROOT<dialog_entertext_c> &data) :gui_isodialog_c(data), m_params(data.prms), watch(data.prms.watch, DELEGATE(this, watchdog))
{
}

dialog_entertext_c::~dialog_entertext_c()
{
    //gui->delete_event(DELEGATE(this, refresh_current_page));
}


/*virtual*/ void dialog_entertext_c::created()
{
    gui->exclusive_input(getrid());
    set_theme_rect(CONSTASTR("main"), false);
    __super::created();
    tabsel(CONSTASTR("1"));
}

void dialog_entertext_c::getbutton(bcreate_s &bcr)
{
    __super::getbutton(bcr);
}

bool dialog_entertext_c::watchdog(RID, GUIPARAM p)
{
    TSDEL(this);
    return true;
}

ts::uint32 dialog_entertext_c::gm_handler(gmsg<ISOGM_APPRISE> &)
{
    if (g_app->F_MODAL_ENTER_PASSWORD)
    {
        if (getroot()) getroot()->set_system_focus(true);
    }
    return 0;
}

bool dialog_entertext_c::showpass_handler(RID, GUIPARAM p)
{
    if (RID txt = find(CONSTASTR("txt")))
        HOLD(txt).as<gui_textedit_c>().set_password_char(p ? 0 : '*');

    return true;
}


/*virtual*/ int dialog_entertext_c::additions(ts::irect &)
{

    descmaker dm(descs);
    dm << 1;

    dm().page_header(m_params.desc);
    dm().vspace(25);
    dm().textfield(ts::wstr_c(), m_params.val, DELEGATE(this, on_edit)).focus(true).passwd(UD_ENTERPASSWORD == m_params.utag).setname(CONSTASTR("txt"));

    if (UD_ENTERPASSWORD == m_params.utag)
    {
        dm().vspace();
        dm().checkb(ts::wstr_c(), DELEGATE(this, showpass_handler), false).setmenu(
            menu_c().add(TTT("Show password", 379), 0, MENUHANDLER(), CONSTASTR("1"))
            );

    }

    return 0;
}

bool dialog_entertext_c::on_edit(const ts::wstr_c &t)
{
    if (!m_params.checker(t)) return false;
    m_params.val = t;
    return true;
}

bool dialog_entertext_c::on_enter_press_func(RID, GUIPARAM)
{
    on_confirm();
    return true;
}

bool dialog_entertext_c::on_esc_press_func(RID, GUIPARAM)
{
    on_close();
    return true;
}


/*virtual*/ void dialog_entertext_c::tabselected(ts::uint32 /*mask*/)
{
    if (RID pb = find(CONSTASTR("txt")))
    {
        gui_textfield_c &tf = HOLD(pb).as<gui_textfield_c>();
        
        tf.register_kbd_callback( DELEGATE( this, on_enter_press_func ), SSK_ENTER, false );
        tf.register_kbd_callback( DELEGATE( this, on_enter_press_func ), SSK_PADENTER, false );
        tf.register_kbd_callback( DELEGATE( this, on_esc_press_func ), SSK_ESC, false );
    }
}



/*virtual*/ bool dialog_entertext_c::sq_evt(system_query_e qp, RID rid, evt_data_s &data)
{
    if (__super::sq_evt(qp, rid, data)) return true;

    //switch (qp)
    //{
    //case SQ_DRAW:
    //    if (const theme_rect_s *tr = themerect())
    //        draw(*tr);
    //    break;
    //}

    return false;
}

/*virtual*/ void dialog_entertext_c::on_confirm()
{
    if (m_params.okhandler)
    {
        gui->exclusive_input(getrid(), false);
        if (!m_params.okhandler(m_params.val, m_params.param))
        {
            gui->exclusive_input(getrid());
            return;
        }
    }
    __super::on_confirm();
}

/*virtual*/ void dialog_entertext_c::on_close()
{
    if (m_params.cancelhandler)
    {
        gui->exclusive_input(getrid(), false);
        if (!m_params.cancelhandler(getrid(), nullptr))
        {
            gui->exclusive_input(getrid());
            return;
        }
    }
    __super::on_close();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// about ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



dialog_about_c::dialog_about_c(initial_rect_data_s &data):gui_isodialog_c(data)
{
}

dialog_about_c::~dialog_about_c()
{
    if (gui)
    {
        gui->delete_event( DELEGATE(this, updanim) );
    }
}

/*virtual*/ void dialog_about_c::created()
{
    gui->exclusive_input(getrid());
    set_theme_rect(CONSTASTR("about"), false);
    __super::created();
    tabsel(CONSTASTR("1"));
}

void dialog_about_c::getbutton(bcreate_s &bcr)
{
    if (bcr.tag == 0)
    {
        bcr.tag = 1;
        __super::getbutton(bcr);
    }
}


/*virtual*/ int dialog_about_c::additions(ts::irect &)
{

    descmaker dm(descs);
    dm << 1;

    ts::wstr_c title(256,true);
    title.set( CONSTWSTR("<p=c><b>") );
    title.append( CONSTWSTR("<a href=\"http://isotoxin.im\">Isotoxin</a>") );
    title.append( CONSTWSTR("</b> ") );
    title.appendcvt( g_app->appver() );
    title.append( CONSTWSTR("<br>Coding, design and sounds by <a href=\"https://github.com/Rotkaermota\">Rotkaermota</a>") );
    title.append( CONSTWSTR("<br>Isotoxin is open-source freeware, licensed under <a href=\"https://github.com/Rotkaermota/Isotoxin/blob/master/LICENSE\">GPL3</a>") );
    title.append( CONSTWSTR("<br>See version history <a href=\"https://github.com/Rotkaermota/Isotoxin/releases\">here</a>") );

    dm().label( title );

    dm().vspace(5);
    dm().hiddenlabel(ts::wstr_c(), 0).setname(CONSTASTR("upd"));
    dm().vspace(5);
    dm().button(ts::wstr_c(), TTT("Check for update",354), DELEGATE(this, check_update_now)).height(35).setname(CONSTASTR("checkupdb"));
    dm().vspace(5);

    title.set( CONSTWSTR("<p=c>Used libs<hr><l><a href=\"https://github.com/irungentoo/toxcore/\">Tox core</a>") );
    title.append( CONSTWSTR(" <a href=\"https://www.opus-codec.org\">Opus audio codec</a>") );
    title.append( CONSTWSTR(" <a href=\"http://vorbis.com\">Ogg Vorbis audio codec</a>"));
    title.append( CONSTWSTR(" <a href=\"https://xiph.org/flac/\">Flac audio codec</a>"));
    title.append( CONSTWSTR(" <a href=\"https://github.com/Rotkaermota/filter_audio\">Irungentoo's filter_audio fork</a>"));
    title.append( CONSTWSTR(" <a href=\"http://webmproject.org\">VP8 video codec</a>") );
    title.append( CONSTWSTR(" <a href=\"http://libsodium.org\">Sodium crypto library</a>") );
    title.append( CONSTWSTR(" <a href=\"http://freetype.org\">Free Type font render library</a>") );
    title.append( CONSTWSTR(" <a href=\"http://zlib.net\">zlib compression library</a>") );
    title.append( CONSTWSTR(" <a href=\"http://www.libpng.org/pub/png/libpng.html\">png image library</a>") );
    title.append( CONSTWSTR(" <a href=\"http://pngquant.org\">pngquant library</a>") );
    title.append( CONSTWSTR(" <a href=\"http://libjpeg.sourceforge.net\">jpg image library</a>") );
    title.append( CONSTWSTR(" <a href=\"http://giflib.sourceforge.net\">The GIFLIB project</a>") );
    title.append( CONSTWSTR(" <a href=\"http://g.oswego.edu/dl/html/malloc.html\">dlmalloc</a>") );
    title.append( CONSTWSTR(" <a href=\"https://github.com/yxbh/FastDelegate\">C++11 fastdelegates</a>") );
    title.append( CONSTWSTR(" <a href=\"http://curl.haxx.se/libcurl\">Curl</a>") );
    title.append( CONSTWSTR("</l><br><br>Adapded libs<hr><l><a href=\"https://bitbucket.org/alextretyak/s3\">Simple Sound System</a>") );
    title.append( CONSTWSTR(" <a href=\"https://github.com/jp9000/libdshowcapture\">DirectShow capture lib</a>") );
    title.append( CONSTWSTR(" <a href=\"http://www.codeproject.com/Articles/11132/Walking-the-callstack\">Walking the callstack</a>") );
    title.append( CONSTWSTR(" <a href=\"https://sqlite.org\">SQLite</a>") );
    title.append( CONSTWSTR("</l><br><br>Other sources and assets used<hr><l><a href=\"http://virtualdub.sourceforge.net/\">VirtualDub</a>") );
    title.append( CONSTWSTR(" <a href=\"https://github.com/sarbian/libsquish\">libsquish</a>") );
    title.append( CONSTWSTR(" <a href=\"http://code.google.com/p/libyuv\">libuv</a>") );
    title.append( CONSTWSTR(" <a href=\"https://github.com/libyal/liblnk\">liblink</a>") );
    title.append( CONSTWSTR(" <a href=\"http://www.efgh.com/software/md5.htm\">md5</a>") );
    title.append( CONSTWSTR(" <a href=\"http://dejavu-fonts.org\">DejaVu fonts</a>") );
    title.append( CONSTWSTR(" <a href=\"http://www.kolobok.us\">Copyright<nbsp>�<nbsp>Aiwan.<nbsp>Kolobok<nbsp>Smiles</a>"));
    
    dm().label( title );

    return 0;
}

void dialog_about_c::updrect_about(const void *rr, int r, const ts::ivec2 &p)
{
    if (1000 == r)
    {
        rectengine_root_c *root = getroot();
        root->draw(p - root->get_current_draw_offset(), pa.bmp.extbody(), ts::irect(0,pa.bmp.info().sz), true );

    } else
        updrect_def(rr,r,p);
    
}

bool dialog_about_c::updanim(RID, GUIPARAM)
{
    if (checking_new_version)
    {
        pa.render();
        DEFERRED_UNIQUE_CALL(0, DELEGATE(this, updanim), nullptr);
    }
    
    if (RID upd = find(CONSTASTR("upd")))
        HOLD(upd).engine().redraw();
    return true;
}

bool dialog_about_c::check_update_now(RID, GUIPARAM)
{
    if (RID b = find(CONSTASTR("checkupdb")))
        b.call_enable(false);

    checking_new_version = true;
    pa.restart();
    updanim(RID(),nullptr);

    set_label_text(CONSTASTR("upd"), ts::wstr_c(CONSTWSTR("<p=c><rect=1000,32,32>")));
    if (RID no = find(CONSTASTR("upd")))
        MODIFY(no).visible(true);

    gui->repos_children(this);

    g_app->b_update_ver(RID(), as_param(AUB_ONLY_CHECK));

    return true;
}

ts::uint32 dialog_about_c::gm_handler(gmsg<ISOGM_NEWVERSION>&nv)
{
    if (RID b = find(CONSTASTR("checkupdb")))
        b.call_enable(true);

    if (!checking_new_version) return 0;
    checking_new_version = false;
    gui->repos_children(this);

    if (nv.error_num == gmsg<ISOGM_NEWVERSION>::E_NETWORK)
    {
        set_label_text(CONSTASTR("upd"), ts::wstr_c(CONSTWSTR("<p=c>")) + maketag_color<ts::wchar>(get_default_text_color(0)) + loc_text(loc_connection_failed));
        if (RID no = find(CONSTASTR("upd")))
            MODIFY(no).visible(true);
        return 0;
    }

    if (nv.ver.is_empty() || nv.error_num != gmsg<ISOGM_NEWVERSION>::E_OK)
    {
        set_label_text( CONSTASTR("upd"), ts::wstr_c(CONSTWSTR("<p=c>")) + maketag_color<ts::wchar>(get_default_text_color(0)) + TTT("Update not found",355) );
        if (RID no = find(CONSTASTR("upd")))
            MODIFY(no).visible(true);
        return 0;
    }

    set_label_text( CONSTASTR("upd"), ts::wstr_c(CONSTWSTR("<p=c>")) + maketag_color<ts::wchar>(get_default_text_color(1)) + (TTT("New version detected: $",357) / ts::to_wstr(nv.ver.as_sptr())) );
    if (RID yes = find(CONSTASTR("upd")))
        MODIFY(yes).visible(true);

    return 0;
}


/*virtual*/ ts::wstr_c dialog_about_c::get_name() const
{
    return APPNAME_CAPTION;
}
/*virtual*/ ts::ivec2 dialog_about_c::get_min_size() const
{
    return ts::ivec2(450, 490);
}

/*virtual*/ bool dialog_about_c::sq_evt(system_query_e qp, RID rid, evt_data_s &data)
{
    if (__super::sq_evt(qp, rid, data)) return true;

    return false;
}


