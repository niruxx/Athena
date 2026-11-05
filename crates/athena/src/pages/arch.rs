//! AUR helper management, PKGBUILD viewing, and mirror previews.
use crate::{actions, context::Context, util};
use adw::prelude::*;
use std::rc::Rc;

pub fn build(ctx:&Rc<Context>)->adw::ToolbarView {
    let view=adw::ToolbarView::new();view.add_top_bar(&adw::HeaderBar::builder().title_widget(&adw::WindowTitle::new("Mirrors & Build Tools","")).build());
    let page=adw::PreferencesPage::new();page.add_css_class("page-enter");
    let is_arch=athena_core::distro::detect_family()==athena_core::distro::DistroFamily::Arch;
    let mirrors=adw::PreferencesGroup::builder().title("Repository mirrors").description("Preview a new mirror order before changing your configuration.").build();
    if is_arch{let ctx2=ctx.clone();mirrors.add(&util::with_icon(util::action_row("Rank mirrors","Compare download performance and review the proposed mirror list.","Preview…",move ||mirror_preview(&ctx2)),"network-server-symbolic"));}
    if ctx.backends.iter().any(|b|b.key()=="dnf"){let ctx2=ctx.clone();mirrors.add(&util::with_icon(util::action_row("Fastest DNF mirror","Let DNF select a mirror based on connection speed.","Enable…",move ||actions::custom(&ctx2,"Enable fastest mirror","Save DNF's fastestmirror option?",vec![athena_core::arch::fastest_mirror()])),"network-transmit-receive-symbolic"));}
    page.add(&mirrors);
    if is_arch {
        let files=adw::PreferencesGroup::builder().title("Inspect package sources").description("Read build files without executing them.").build();
        let name=adw::EntryRow::builder().title("Package name").build();let aur=adw::SwitchRow::builder().title("Use the AUR").subtitle("Otherwise, look in Arch's official package sources.").build();files.add(&name);files.add(&aur);
        let ctx2=ctx.clone();files.add(&util::with_icon(util::action_row("Build files","Open the PKGBUILD and accompanying sources for this package.","View…",move ||build_files(&ctx2,&name.text(),aur.is_active())),"text-x-generic-symbolic"));page.add(&files);
        let helpers=adw::PreferencesGroup::builder().title("Optional AUR helpers").description("Athena reviews and builds packages directly with makepkg.").build();
        for helper in ["paru","yay"] {
            let row=adw::ExpanderRow::builder().title(helper).subtitle("Manage the helper separately from Athena's reviewed build flow.").build();let icon=gtk::Image::builder().icon_name("applications-utilities-symbolic").valign(gtk::Align::Center).build();icon.add_css_class("permission-icon");row.add_prefix(&icon);
            let install_ctx=ctx.clone();let install=util::action_row("Install or update","Review the helper's AUR build files before installing.","Review…",move ||crate::aur_review::review(&install_ctx,vec![helper.into()]));install.set_margin_start(6);row.add_row(&install);
            let remove_ctx=ctx.clone();let remove=util::action_row("Remove helper","Uninstall the helper through pacman.","Remove…",move ||{if let Some(source)=remove_ctx.backends.iter().position(|b|b.key()=="pacman"){actions::transaction(&remove_ctx,source,actions::Kind::Remove,vec![helper.into()]);}});row.add_row(&remove);helpers.add(&row);
        }page.add(&helpers);
    }
    view.set_content(Some(&page));view
}
pub fn show_news(ctx:&Rc<Context>){
    let Some(feed)=athena_core::arch::news_feed() else{return;};
    ctx.navigation.push(&adw::NavigationPage::builder().title(feed.name).child(&crate::pages::news::build(ctx,feed)).build());
}

pub fn gate_update(ctx: &Rc<Context>, accepted: impl FnOnce() + 'static) {
    if !ctx.backends.iter().any(|b| b.key() == "pacman") {
        accepted();
        return;
    }
    let ctx = ctx.clone();
    util::background(athena_core::arch::news, move |result| {
        let read = crate::settings::load()
            .map(|s| s.strv("arch-news-read").iter().map(|s| s.to_string()).collect::<Vec<_>>())
            .unwrap_or_default();
        let body = match result {
            Ok(news) => {
                let unread = news.into_iter().filter(|n| !read.contains(&n.id)).map(|n| n.title).collect::<Vec<_>>();
                if unread.is_empty() {
                    accepted();
                    return;
                }
                format!("Read these notices before a full system update:\n\n{}", unread.join("\n"))
            }
            Err(error) => format!("Could not check Arch news. You can retry or explicitly update anyway.\n\n{error}"),
        };
        let dialog = adw::AlertDialog::new(Some(&crate::i18n::gettext("Check Arch News Before Updating")), Some(&body));
        dialog.add_responses(&[
            ("cancel", &crate::i18n::gettext("Cancel")),
            ("read", &crate::i18n::gettext("Read First")),
            ("update", &crate::i18n::gettext("Update Anyway")),
        ]);
        dialog.set_close_response("cancel");
        let accepted = std::cell::RefCell::new(Some(accepted));
        let ctx2 = ctx.clone();
        dialog.connect_response(None, move |_, response| {
            if response == "read" {
                show_news(&ctx2);
            } else if response == "update"
                && let Some(accepted) = accepted.borrow_mut().take()
            {
                accepted();
            }
        });
        dialog.present(Some(&ctx.window));
    });
}

pub fn build_files(ctx: &Rc<Context>, name: &str, is_aur: bool) {
    let dialog = adw::Dialog::builder()
        .title(crate::i18n::gettext("Package Build Files"))
        .content_width(850)
        .content_height(650)
        .build();
    let toolbar = adw::ToolbarView::new();
    toolbar.add_top_bar(&adw::HeaderBar::new());
    toolbar.set_content(Some(&util::loading_page("Fetching build files without running them…")));
    dialog.set_child(Some(&toolbar));
    dialog.present(Some(&ctx.window));
    let name = name.to_owned();
    util::background(
        move || athena_core::aur::view_build_files(&name, is_aur),
        move |result| match result {
            Ok(files) => {
                let stack = gtk::Stack::new();
                let picker = gtk::DropDown::from_strings(&files.iter().map(|f| f.name.as_str()).collect::<Vec<_>>());
                picker.set_margin_top(6);picker.set_margin_bottom(6);picker.set_margin_start(12);picker.set_margin_end(12);
                for file in files {
                    let text = gtk::TextView::builder().editable(false).monospace(true).build();
                    text.buffer().set_text(&file.text);
                    stack.add_named(&gtk::ScrolledWindow::builder().child(&text).build(), Some(&file.name));
                }
                let target = stack.clone();
                picker.connect_selected_notify(move |picker| {
                    if let Some(name) = picker.selected_item().and_downcast::<gtk::StringObject>() {
                        target.set_visible_child_name(&name.string());
                    }
                });
                toolbar.add_top_bar(&picker);
                toolbar.set_content(Some(&stack));
            }
            Err(error) => toolbar.set_content(Some(
                &adw::StatusPage::builder()
                    .title(crate::i18n::gettext("Could Not Fetch Build Files"))
                    .description(&error)
                    .build(),
            )),
        },
    );
}
fn mirror_preview(ctx: &Rc<Context>) {
    let dialog = adw::Dialog::builder()
        .title(crate::i18n::gettext("Mirror Ranking Preview"))
        .content_width(760)
        .content_height(600)
        .build();
    let toolbar = adw::ToolbarView::new();
    toolbar.add_top_bar(&adw::HeaderBar::new());
    toolbar.set_content(Some(&util::loading_page("Ranking mirrors…")));
    dialog.set_child(Some(&toolbar));
    dialog.present(Some(&ctx.window));
    let path = gtk::glib::user_cache_dir().join(format!(
        "athena/mirrors-{}.list",
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default()
            .as_nanos()
    ));
    let path2 = path.clone();
    let ctx = ctx.clone();
    util::background(
        move || {
            std::fs::create_dir_all(path2.parent().unwrap()).map_err(|e| e.to_string())?;
            athena_core::arch::rank_mirrors(&path2)
        },
        move |result| match result {
            Ok(text) => {
                let body = gtk::Box::builder().orientation(gtk::Orientation::Vertical).spacing(8).build();
                let preview = gtk::TextView::builder().editable(false).monospace(true).build();
                preview.buffer().set_text(&text);
                body.append(&gtk::ScrolledWindow::builder().child(&preview).vexpand(true).build());
                let save = gtk::Button::builder().label(crate::i18n::gettext("Save Mirror List (Keep Backup)…")).halign(gtk::Align::End).margin_top(6).margin_bottom(12).margin_end(12).build();
                save.add_css_class("suggested-action");save.add_css_class("pill");
                save.connect_clicked(move |_| {
                    dialog.close();
                    actions::custom(
                        &ctx,
                        "Save ranked mirrors",
                        "The existing mirror list is backed up before saving the preview.",
                        athena_core::arch::mirror_save_commands(&path),
                    );
                });
                body.append(&save);
                toolbar.set_content(Some(&body));
            }
            Err(error) => toolbar.set_content(Some(
                &adw::StatusPage::builder()
                    .title(crate::i18n::gettext("Could Not Rank Mirrors"))
                    .description(&error)
                    .build(),
            )),
        },
    );
}
