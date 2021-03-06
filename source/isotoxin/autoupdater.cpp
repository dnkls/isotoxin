#include "isotoxin.h"

#ifdef _WIN32
#define _NTOS_
#pragma push_macro("ERROR")
#undef ERROR
//#include <winsock2.h> // ntol
#include "curl/include/curl/curl.h"
#pragma pop_macro("ERROR")
#pragma warning (push)
#pragma warning (disable:4324)
#include "libsodium/src/libsodium/include/sodium.h"
#pragma warning (pop)
#endif // _WIN32
#ifdef _NIX
#include <sodium.h>
#include <curl/curl.h>
#endif // _NIX

#include "../shared/shared.h"

#ifdef MODE64
#define UPDATE64 true
#define PROP(a) CONSTASTR(a "-64")
#else
#define UPDATE64 (update64 ? ts::is_64bit_os() : false)
#define PROP(a) ts::str_c( CONSTASTR(a) ).append( update64 ? CONSTASTR("-64") : CONSTASTR("") )
#endif

extern ts::static_setup<spinlock::syncvar<autoupdate_params_s>,1000> auparams;

size_t header_callback(char *buffer, size_t size, size_t nitems, void *userdata)
{
    return size * nitems;
}

size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    ts::buf_c *resultad = (ts::buf_c *)userdata;
    resultad->append_buf(ptr, size * nmemb);
    return size * nmemb;
}

int ver_ok( ts::asptr verss )
{
    ts::pstr_c ss(verss);
    int signi = ss.find_pos(CONSTASTR("\r\nsign="));
    if (signi < 0)
    {
        // \r\n not found... may be file was converted to \n format?
        // so
        ts::token<char> vv(verss, '\n');
        ts::str_c winverss;
        for(;vv;++vv)
            winverss.append( vv->get_trimmed() ).append(CONSTASTR("\r\n"));
        winverss.trunc_length(2); // remove last \r\n
        return winverss.get_length() == verss.l ? 0 : ver_ok(winverss.as_sptr());
    }
    if ((ss.get_length() - signi - 7) != crypto_sign_BYTES * 2)
        return 0;

    ts::uint8 sig[crypto_sign_BYTES];
    ts::uint8 pk[crypto_sign_PUBLICKEYBYTES] = {
#include "signpk.inl"
    };
    ss.hex2buf<crypto_sign_BYTES>(sig, signi + 7);

    if  (0 == crypto_sign_verify_detached(sig, (const ts::uint8 *)verss.s, signi, pk))
        return signi;

    return 0;
}

auto getss = [](const ts::asptr &latest, const ts::asptr&t) ->ts::asptr
{
    ts::token<char> ver( latest, '\n' );
    for (;ver; ++ver)
    {
        auto s = ver->get_trimmed();
        if (s.begins(t) && s.get_char(t.l) == '=')
        {
            return s.substr(t.l + 1).get_trimmed().as_sptr();
        }
    }
    return ts::asptr();
};

bool blake2b_ok(ts::buf_c &b, const ts::asptr &latest, bool update64 )
{
    ts::str_c blake2bs( getss(latest, PROP("blake2b")) );
    if (blake2bs.get_length() != BLAKE2B_HASH_SIZE * 2) return false;
    if (ts::pstr_c(getss(latest, PROP("size"))).as_int() != b.size()) return false;
    ts::uint8 hash[BLAKE2B_HASH_SIZE];
    BLAKE2B(hash, b.data(), b.size());
    for (int i = 0; i < 16; ++i)
        if (hash[i] != blake2bs.as_byte_hex(i * 2))
            return false;
    return true;
}

namespace
{
    struct dnver_s
    {
        ts::str_c ver;      // if empty - not downloaded
        ts::wstr_c fn32;    // if non-empty, then 32-bit version present and ok
        ts::wstr_c fn64;    // if non-empty, then 64-bit version present and ok

        bool check( bool update64 )
        {
            return update64 ? !fn64.is_empty() : !fn32.is_empty();
        }
    };
}

void get_downloaded_ver( dnver_s &dnver )
{
    dnver.ver.clear();

    if ( auparams().lock_read()().need_setup_paths() )
        auparams().lock_write()().setup_paths();

    for (int pin = 0;pin < autoupdate_params_s::NUM_PATHS;++pin)
    {
        ts::buf_c bbb;
        bbb.load_from_disk_file(ts::fn_join(auparams().lock_read()().paths[pin], CONSTWSTR("latest.txt")));
        if (bbb.size() > 0)
        {
            int signi = ver_ok(bbb.cstr());
            if (!signi) return;
            ts::str_c latest(bbb.cstr().part(signi));

            ts::wstr_c wurl(from_utf8(getss(latest, CONSTASTR("url"))));
            ts::wstr_c fn = ts::fn_join(auparams().lock_read()().paths[pin], ts::fn_get_name_with_ext(wurl));
            bbb.load_from_disk_file(fn);
            if (blake2b_ok(bbb, latest, false))
            {
                dnver.ver = getss(latest, CONSTASTR("ver"));
                dnver.fn32 = fn;
            }

            wurl = from_utf8(getss(latest, CONSTASTR("url-64")));
            if (!wurl.is_empty())
            {
                fn = ts::fn_join(auparams().lock_read()().paths[pin], ts::fn_get_name_with_ext(wurl));
                bbb.load_from_disk_file(fn);
                if (blake2b_ok(bbb, latest, true))
                {
                    if (dnver.ver.is_empty())
                        dnver.ver = getss(latest, CONSTASTR("ver"));
                    dnver.fn64 = fn;
                    break;
                }
            }
        }
    }
}

#ifdef _NIX
#include "win32emu/win32emu.h"
#endif // _NIX

namespace
{
    struct myprogress_s
    {
        int lastruntime = GetTickCount();
        CURL *curl;
        int id;
        myprogress_s(CURL *c, int id):curl(c), id(id) {}
    };
    static int xferinfo(void *p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
    {
        myprogress_s *myp = (myprogress_s *)p;

        int curtime = GetTickCount();

        if((curtime - myp->lastruntime) >= 500)
        {
            // every 1000 ms
            myp->lastruntime = curtime;
            TSNEW(gmsg<ISOGM_DOWNLOADPROGRESS>, myp->id, (int)dlnow, (int)dltotal)->send_to_main_thread();
        }

        return 0;
    }
}

int curl_execute_download(CURL *curl, int id)
{
    myprogress_s progress(curl, id);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfo);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
    return curl_easy_perform(curl);
}

void set_proxy_curl( CURL *curl, int proxy_type, const ts::asptr &proxy_addr )
{
    if (proxy_type > 0)
    {
        ts::str_c proxya(proxy_addr);

        int pt = 0;
        if (proxy_type == 1) pt = CURLPROXY_HTTP;
        else if (proxy_type == 2 ) pt = CURLPROXY_SOCKS4;
        else if (proxy_type == 3 ) pt = CURLPROXY_SOCKS5_HOSTNAME;

        curl_easy_setopt(curl, CURLOPT_PROXY, proxya.cstr());
        //curl_easy_setopt(curl, CURLOPT_PROXYPORT, port);
        curl_easy_setopt(curl, CURLOPT_PROXYTYPE, pt);
    }
}

void autoupdater()
{
    MEMT( MEMT_AUTOUPDATER );

    ts::astrings_c addresses;

    auto aar = auparams().lock_read();
    if (aar().in_updater)
        return;

#ifdef MODE64
    bool update64 = true;
#else
    bool update64 = !aar().disable64;
    update64 = UPDATE64;
#endif // MODE64

    ts::str_c lurl = aar().dbgoptions.get(CONSTASTR("local_upd_url"));
    bool ignore_proxy_aurl = false;
    bool only_aurl = false;
    if (!lurl.is_empty())
    {
        lurl.clone();
        addresses.add(lurl);
        ignore_proxy_aurl = aar().dbgoptions.get(CONSTASTR("ignproxy")).as_uint() != 0;
        only_aurl = aar().dbgoptions.get(CONSTASTR("onlythisurl")).as_uint() != 0;
#ifdef _DEBUG
        ignore_proxy_aurl = true;
#endif
    }
    aar.unlock();

    if (!only_aurl)
    {
        addresses.add("https://github.com/isotoxin/isotoxin/wiki/latest");
        addresses.add( HOME_SITE "/latest.txt");
    }
    int addri = 0;

    struct curl_s
    {
        CURL *curl;
        ts::str_c newver;
        gmsg<ISOGM_NEWVERSION>::error_e error_num = gmsg<ISOGM_NEWVERSION>::E_OK;
        bool new64 = false;
        bool send_newver = false;
        curl_s()
        {
            curl = curl_easy_init();
            auto w = auparams().lock_write();
            w().in_progress = true;
            w().in_updater = true;
        }
        ~curl_s()
        {
            if (curl) curl_easy_cleanup(curl);
            auto w = auparams().lock_write();
            w().in_progress = false;
            w().in_updater = false;
            w.unlock();

            if (send_newver)
                TSNEW( gmsg<ISOGM_NEWVERSION>, newver, error_num, new64 )->send_to_main_thread();
        }
        operator CURL *() {return curl;}
    } curl;

    bool ok_sign = false;

    if (!curl) return;
    ts::buf_c d;

    ts::str_c latest_ver_available;

next_address:
    if (addri >= addresses.size())
    {
        curl.send_newver = true;
        if (ok_sign)
            curl.newver = latest_ver_available;
        else
            curl.error_num = gmsg<ISOGM_NEWVERSION>::E_NETWORK;
        return;
    }

    d.clear();

    int rslt = 0;
    rslt = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &d);
    rslt = curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    rslt = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);

    if (addri == 0 && !lurl.is_empty())
    {
        if (ignore_proxy_aurl)
            auparams().lock_write()().proxy_type = 0;
    } else
    {
#ifdef _DEBUG
        auparams().lock_write()().proxy_addr = CONSTASTR("srv:9050");
        auparams().lock_write()().proxy_type = 3;
#endif
    }

    set_common_curl_options(curl);

    auto r = auparams().lock_read();
    set_proxy_curl( curl, r().proxy_type, r().proxy_addr.as_sptr() );
    r.unlock();

    rslt = curl_easy_setopt(curl, CURLOPT_URL, addresses.get(addri).cstr());
    rslt = curl_easy_perform(curl);

    // extract info from github wiki
    int begin_wiki = ts::pstr_c(d.cstr()).find_pos(CONSTASTR("[begin]"));
    if ( begin_wiki > 0 )
    {
        int end_wiki = ts::pstr_c(d.cstr()).find_pos(begin_wiki + 7,CONSTASTR("[end]"));
        if (end_wiki > 0)
        {
            ts::str_c preop( ts::pstr_c(d.cstr()).substr(begin_wiki + 7, end_wiki) );
            preop.replace_all(CONSTASTR("\r"), CONSTASTR(""));
            preop.replace_all(CONSTASTR("\n"), CONSTASTR(""));
            preop.replace_all(CONSTASTR("<br><code>"), CONSTASTR(""));
            preop.replace_all(CONSTASTR("</code><br>"), CONSTASTR("\n"));
            preop.replace_all(CONSTASTR("</code>"), CONSTASTR("\n"));
            ts::str_c newver;
            for(ts::token<char> t(preop, '\n');t;++t)
                newver.append(*t).append(CONSTASTR("\r\n"));
            if (newver.get_length())
            {
                d.set_size(newver.get_length() - 2); // -2 - do not copy last \r\n
                memcpy(d.data(), newver.cstr(), newver.get_length() - 2);
            }
        }
    }

    int signi = ver_ok( d.cstr() );
    if (!signi)
    {
        ++addri;
        goto next_address;
    }

    ok_sign = true;

    ts::str_c latests( d.cstr().s, signi );

    ts::str_c alt(getss(latests, CONSTASTR("alt")));
    if (!alt.is_empty())
    {
        for( ts::token<char> t(alt.as_sptr(),'@'); t; ++t )
            addresses.get_string_index(t->as_sptr());
    }

    bool same64 = false;
    curl.new64 = true;
#ifndef MODE64
    same64 = UPDATE64 && getss( latests, CONSTASTR( "url-64" ) ).l > 0;
    curl.new64 = same64;
#endif

    r = auparams().lock_read();
    const ts::str_c aver1( getss(latests, CONSTASTR("ver")) );
    if (!new_version( r().ver, aver1, same64 ))
    {
        if (new_version(latest_ver_available, aver1, false))
            latest_ver_available.set(aver1);

        ++addri;
        r.unlock();
        goto next_address;
    }

    r.unlock();

    bool downloaded = false;
    dnver_s dnver;
    get_downloaded_ver( dnver );
    ts::str_c aver(getss(latests, CONSTASTR("ver")));
    if (dnver.ver == aver && dnver.check( update64 ))
        downloaded = true;

    r = auparams().lock_read();
    if (downloaded || r().autoupdate == AUB_ONLY_CHECK)
    {
        r.unlock();
        if (downloaded)
            auparams().lock_write()().downloaded = true;

        // just notify
        curl.newver = aver;
        curl.send_newver = true;
        return;
    }
    r.unlock();

    ts::str_c aurl( getss(latests, PROP("url")) );
    ts::wstr_c pakname = ts::fn_get_name_with_ext(from_utf8(aurl));
    ts::str_c address = addresses.get(addri);
    if (aurl.get_char(0) == '/')
    {
        address.set_length(address.find_pos(7, '/')).append(aurl).trim();
    } else
    {
        address = aurl;
    }

    ts::buf_c latest(d);
    d.clear();
    rslt = curl_easy_setopt(curl, CURLOPT_URL, address.cstr());
    rslt = curl_easy_setopt(curl, CURLOPT_USERAGENT, USERAGENT);
    rslt = curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);

    curl_execute_download(curl,-1);

    if (d.size() > 1000000) // downloaded size mus be greater than 1m bytes
    {
        TSNEW(gmsg<ISOGM_DOWNLOADPROGRESS>, -1, (int)d.size(), (int)d.size())->send_to_main_thread();
    }

    if (!blake2b_ok(d, latests, update64))
    {
        ++addri;
        goto next_address;
    }

    ts::make_path( auparams().lock_read()().paths[0], 0 );
    if (!latest.save_to_file( ts::fn_join( auparams().lock_read()().paths[0], CONSTWSTR("latest.txt") ), 0, true ))
    {
        curl.error_num = gmsg<ISOGM_NEWVERSION>::E_DISK;
        return;
    }

    if (!d.save_to_file( ts::fn_join( auparams().lock_read()().paths[0], pakname ), 0, true ))
    {
        curl.error_num = gmsg<ISOGM_NEWVERSION>::E_DISK;
        return;
    }

    auparams().lock_write()().downloaded = true;
    curl.error_num = gmsg<ISOGM_NEWVERSION>::E_OK_FORCE;
    curl.newver = aver;
    curl.send_newver = true;
}

namespace {
struct updater
{
    updater(const ts::wstr_c &exe_path):exe_path(exe_path) {}
    ts::wstr_c exe_path;
    time_t amfn = ts::now();
    bool updfail = false;
    ts::wstrings_c moved;

    bool process_pak_file(const ts::arc_file_s &f)
    {
        ts::wstr_c wfn(ts::fn_join(exe_path,ts::to_wstr(f.fn)));
        ts::wstr_c ff(ts::fn_join(exe_path, CONSTWSTR("old")));
        ff.append_char(NATIVE_SLASH).append_as_num<time_t>(amfn).append_char(NATIVE_SLASH);
        ts::make_path(ff, 0);

        if (ts::ren_or_move_file(wfn, ts::fn_join(ff, ts::fn_get_name_with_ext(wfn))))
        {
            moved.add(wfn);

        } else if (ts::is_file_exists(wfn))
        {
            // update failed
            // rollback

            rollback:
            updfail = true;
            // oops
            for (const ts::wstr_c &mf : moved)
            {
                ts::kill_file(mf); // delete new file
                ts::ren_or_move_file(ts::fn_join(ff, mf), mf); // return moved one
            }
            return false;
        }

        if (!f.get().save_to_file(wfn, 0, true))
            goto rollback;

        return true;
    }
};
}

bool check_autoupdate()
{
    MEMT( MEMT_AUTOUPDATER );

    bool is_64 = false;
    bool update64 = false;

#ifdef MODE64
    is_64 = true;
    update64 = true;
#else
    update64 = true;
    update64 = UPDATE64;
#endif // MODE64

    dnver_s dnver;
    get_downloaded_ver( dnver );
    if (dnver.ver.is_empty()) return true;

    ts::wstr_c path_exe(ts::fn_get_path(ts::get_exe_full_name()));
    ts::wstr_c fn = is_64 ? dnver.fn64 : ( update64 && !dnver.fn64.is_empty() ? dnver.fn64 : dnver.fn32 );

    if (application_c::appver() == dnver.ver)
    {
        if ( !is_64 && update64 && dnver.check( true ) ) // there are 64 bit version downloaded with same version as current 32 bit version and current OS is 64 bit
        {
            fn = dnver.fn64;
            goto do_update;
        }

        if ( ( is_64 && dnver.check( true ) ) ||
             (!is_64 && dnver.check( false ))
            )
        {
            // just cleanup

            for (int pin = 0; pin < autoupdate_params_s::NUM_PATHS; ++pin)
            {

                ts::wstr_c ff(auparams().lock_read()().paths[pin]);
                if (is_dir_exists(ff))
                    del_dir(ff);

                ff = ts::fn_join(path_exe, CONSTWSTR("old"));
                if (is_dir_exists(ff) && check_write_access(ff))
                    del_dir(ff);
            }

            return true;
        }

    } else if (new_version(application_c::appver(), dnver.ver ))
    {
    do_update:

        ts::buf_c pak;
        pak.load_from_disk_file(fn);
        if ( pak.size() < 1024 )
            return true;

        if (!check_write_access(path_exe))
            return true; // can't write to exe folder - do nothing

        del_dir(ts::fn_join(path_exe, CONSTWSTR("old")));

        updater u(path_exe);
        ts::zip_open(pak.data(), pak.size(), DELEGATE(&u,process_pak_file));

        if (u.updfail)
            return true; // continue run

        ts::master().start_app(ts::wstr_c(CONSTWSTR("isotoxin")), ts::wstr_c(), nullptr, false);

        return false;
    }

    return true;
}

