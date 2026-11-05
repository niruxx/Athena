//! Installed software, with wrapping source chips and native group membership.
use std::{cell::{Cell,RefCell},rc::Rc};
use adw::prelude::*;
use crate::{context::{Context,Item},package_list::PackageList,util};

pub struct InstalledPage {
    pub widget:adw::ToolbarView,
    pub search:gtk::SearchEntry,
    pub groups:gtk::DropDown,
    list:Rc<PackageList>,stack:gtk::Stack,title:adw::WindowTitle,ctx:Rc<Context>,
    apps:gtk::ToggleButton,show_all:gtk::Button,group_generation:Cell<u64>,
}
impl InstalledPage {
    pub fn new(ctx:&Rc<Context>)->Rc<Self>{
        let title=adw::WindowTitle::new(&crate::i18n::gettext("Installed"),"");
        let header=adw::HeaderBar::builder().title_widget(&title).build();
        let refresh=gtk::Button::builder().icon_name("view-refresh-symbolic").tooltip_text("Reload installed packages").build();header.pack_end(&refresh);
        let search=gtk::SearchEntry::builder().placeholder_text("Filter installed software").hexpand(true).build();
        let filters=gtk::Box::builder().orientation(gtk::Orientation::Vertical).spacing(12).margin_top(10).margin_bottom(12).margin_start(18).margin_end(18).build();filters.append(&search);
        let apps=gtk::ToggleButton::with_label(&crate::i18n::gettext("Apps only"));apps.add_css_class("filter-chip");apps.set_valign(gtk::Align::Center);
        let groups=gtk::DropDown::builder().model(&gtk::StringList::new(&["All groups"])).enable_search(true).valign(gtk::Align::Center).tooltip_text("See what you've installed per group").build();
        let group_choices=Rc::new(RefCell::new(Vec::<(usize,athena_core::PackageGroupInfo)>::new()));
        let group_label=gtk::Box::builder().spacing(6).build();group_label.append(&gtk::Image::from_icon_name("folder-symbolic"));group_label.append(&util::label("Group",&["dim-label"]));
        let source_label=gtk::Box::builder().spacing(6).margin_start(6).build();source_label.append(&gtk::Image::from_icon_name("network-server-symbolic"));source_label.append(&util::label("Source",&["dim-label"]));
        let show_all=gtk::Button::builder().visible(false).halign(gtk::Align::Start).build();let toggle=apps.clone();show_all.connect_clicked(move |_|toggle.set_active(false));
        let list=Rc::new(PackageList::new(ctx));let stack=util::animated_stack();stack.add_named(&util::loading_page("Loading installed packages…"),Some("loading"));stack.add_named(&list.widget,Some("list"));
        stack.add_named(&adw::StatusPage::builder().icon_name("system-search-symbolic").title("No matches").description("Try another source, group, or search term.").build(),Some("empty"));
        let widget=adw::ToolbarView::new();widget.add_top_bar(&header);widget.add_top_bar(&adw::Clamp::builder().maximum_size(1100).child(&filters).build());widget.set_content(Some(&stack));
        let page=Rc::new(Self{widget,search:search.clone(),groups:groups.clone(),list,stack,title,ctx:ctx.clone(),apps:apps.clone(),show_all:show_all.clone(),group_generation:Cell::new(0)});
        let weak=Rc::downgrade(&page);let sources=util::source_dropdown(ctx,move |source|{if let Some(page)=weak.upgrade(){page.list.set_source(source);page.update_view();}});
        let options=gtk::FlowBox::builder().selection_mode(gtk::SelectionMode::None).column_spacing(10).row_spacing(8).max_children_per_line(6).build();options.add_css_class("filter-bar");
        let source_cell=gtk::Box::builder().spacing(6).build();source_cell.append(&source_label);source_cell.append(&sources);
        let group_cell=gtk::Box::builder().spacing(6).build();group_cell.append(&group_label);group_cell.append(&groups);
        options.insert(&apps,-1);options.insert(&source_cell,-1);options.insert(&group_cell,-1);
        let has_groups=ctx.backends.iter().any(|b|matches!(b.key(),"pacman"|"apt"|"dnf"));group_cell.set_visible(has_groups);
        filters.append(&options);filters.append(&show_all);
        {let page=page.clone();search.connect_search_changed(move |entry|{page.list.set_text(&entry.text());page.update_view();});}
        {let page=page.clone();apps.connect_toggled(move |toggle|{page.list.set_apps_only(toggle.is_active());page.update_view();});}
        {let page=page.clone();refresh.connect_clicked(move |_|page.reload());}
        {let weak=Rc::downgrade(&page);ctx.on_packages_changed(move ||{if let Some(page)=weak.upgrade(){page.reload();}});}
        {
            let page=page.clone();let choices=group_choices.clone();
            groups.connect_selected_notify(move |choice|{
                let revision=page.group_generation.get()+1;page.group_generation.set(revision);
                if choice.selected()==0 {page.list.set_group(None);page.update_view();return;}
                let Some((source,group))=choices.borrow().get(choice.selected() as usize-1).cloned()else{return;};
                let backend=page.ctx.backends[source].clone();let target=page.clone();page.stack.set_visible_child_name("loading");
                util::background(move ||{let mut details=backend.group_details(&group.id,group.is_meta);for child in details.sub_groups.clone(){details.packages.extend(backend.group_details(&child,false).packages);}details.packages.into_iter().collect()},move |names|{
                    if target.group_generation.get()!=revision{return;}target.list.set_group(Some((source,names)));target.update_view();
                });
            });
        }
        let backends=ctx.backends.clone();let choices=group_choices.clone();let picker=groups.clone();
        util::background(move ||backends.iter().enumerate().filter(|(_,b)|matches!(b.key(),"pacman"|"apt"|"dnf")).flat_map(|(s,b)|b.list_groups().into_iter().map(move |g|(s,g))).collect::<Vec<_>>(),move |mut found|{
            found.sort_by_key(|(_,g)|(!g.installed,g.name.to_lowercase()));let mut labels=vec![crate::i18n::gettext("All groups")];labels.extend(found.iter().map(|(_,g)|format!("{}{}",g.name,if g.installed{"  ✓"}else{""})));*choices.borrow_mut()=found;picker.set_model(Some(&gtk::StringList::new(&labels.iter().map(String::as_str).collect::<Vec<_>>())));
        });
        page.reload();page
    }
    fn update_view(&self){
        let hidden=self.list.hidden_apps_count();self.show_all.set_label(&format!("Show all ({hidden} hidden by Apps only)"));self.show_all.set_visible(self.apps.is_active());let(shown,total)=(self.list.visible_count(),self.list.total_count());self.stack.set_visible_child_name(if shown==0&&total>0{"empty"}else{"list"});self.title.set_subtitle(&if shown==total{format!("{total} packages")}else{format!("{shown} of {total} packages")});
    }
    pub fn reload(self:&Rc<Self>){
        if self.list.total_count()==0{self.stack.set_visible_child_name("loading");}let backends=self.ctx.backends.clone();let page=self.clone();
        util::background(move ||backends.iter().map(|b|b.list_installed()).collect::<Vec<_>>(),move |per_source|{
            let aur_names:std::collections::HashSet<_>=per_source.iter().enumerate().filter(|(source,_)|page.ctx.backends[*source].key()=="aur").flat_map(|(_,packages)|packages.iter().map(|p|p.name.clone())).collect();
            let mut items:Vec<Item>=per_source.into_iter().enumerate().flat_map(|(source,packages)|packages.into_iter().map(move |package|Item{source,package})).collect();items.retain(|item|page.ctx.backends[item.source].key()!="pacman"||!aur_names.contains(&item.package.name));items.sort_by_key(|item|item.package.name.to_lowercase());page.list.set_items(items);page.update_view();
        });
    }
}
