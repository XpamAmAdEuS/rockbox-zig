use crate::api::rockbox::v1alpha1::browse_service_client::BrowseServiceClient;
use crate::api::rockbox::v1alpha1::settings_service_client::SettingsServiceClient;
use crate::api::rockbox::v1alpha1::{
    GetGlobalSettingsRequest, GetGlobalSettingsResponse, TreeGetEntriesRequest,
    TreeGetEntriesResponse,
};
use crate::state::AppState;
use crate::ui::file::File;
use adw::prelude::*;
use adw::subclass::prelude::*;
use anyhow::Error;
use glib::subclass;
use gtk::glib;
use gtk::{Button, CompositeTemplate, ListBox};
use std::cell::RefCell;
use std::env;
use std::path::Path;

mod imp {

    use super::*;

    #[derive(Debug, Default, CompositeTemplate)]
    #[template(resource = "/io/github/tsirysndr/Rockbox/gtk/files.ui")]
    pub struct Files {
        #[template_child]
        pub files: TemplateChild<ListBox>,

        pub main_stack: RefCell<Option<adw::ViewStack>>,
        pub go_back_button: RefCell<Option<Button>>,
        pub music_directory: RefCell<Option<String>>,
        pub state: glib::WeakRef<AppState>,
    }

    #[glib::object_subclass]
    impl ObjectSubclass for Files {
        const NAME: &'static str = "Files";
        type ParentType = gtk::Box;
        type Type = super::Files;

        fn class_init(klass: &mut Self::Class) {
            Self::bind_template(klass);
        }

        fn instance_init(obj: &subclass::InitializingObject<Self>) {
            obj.init_template();
        }
    }

    impl ObjectImpl for Files {
        fn constructed(&self) {
            self.parent_constructed();

            let self_weak = self.downgrade();
            glib::idle_add_local(move || {
                let self_ = match self_weak.upgrade() {
                    Some(self_) => self_,
                    None => return glib::ControlFlow::Continue,
                };

                glib::spawn_future_local(async move {
                    let obj = self_.obj();
                    obj.load_files(None);
                });

                let self_ = match self_weak.upgrade() {
                    Some(self_) => self_,
                    None => return glib::ControlFlow::Continue,
                };

                glib::ControlFlow::Break
            });
        }
    }

    impl WidgetImpl for Files {}
    impl BoxImpl for Files {}

    impl Files {
        pub fn set_main_stack(&self, main_stack: adw::ViewStack) {
            *self.main_stack.borrow_mut() = Some(main_stack);
        }

        pub fn set_go_back_button(&self, go_back_button: Button) {
            *self.go_back_button.borrow_mut() = Some(go_back_button);
        }
    }
}

glib::wrapper! {
  pub struct Files(ObjectSubclass<imp::Files>)
    @extends gtk::Widget, gtk::Box;
}

#[gtk::template_callbacks]
impl Files {
    pub fn new() -> Self {
        glib::Object::new()
    }

    pub fn load_files(&self, path: Option<String>) {
        let rt = tokio::runtime::Runtime::new().unwrap();
        let response_ = rt.block_on(async {
            let url = build_url();
            let mut client = BrowseServiceClient::connect(url).await?;
            let response = client
                .tree_get_entries(TreeGetEntriesRequest { path: path.clone() })
                .await?
                .into_inner();
            Ok::<TreeGetEntriesResponse, Error>(response)
        });

        if let Ok(response) = response_ {
            let files = self.imp().files.clone();
            while let Some(row) = files.first_child() {
                files.remove(&row);
            }
            let state = self.imp().state.upgrade().unwrap();
            state.set_current_path(path.clone().unwrap_or("".to_string()).as_str());

            for entry in response.entries {
                let file = File::new();
                // pop up the filename
                let filename = entry.name.split("/").last().unwrap();
                file.imp().set_files(self.imp().files.clone());
                file.imp()
                    .set_go_back_button(self.imp().go_back_button.borrow().clone());
                file.imp().file_name.set_text(filename);
                file.imp().state.set(Some(&state));
                file.imp().set_path(entry.name.clone());
                file.imp().set_is_dir(entry.attr == 16);

                match entry.attr == 16 {
                    true => {
                        file.imp().file_menu.set_visible(false);
                        file.imp().directory_menu.set_visible(true);
                    }
                    false => {
                        file.imp().file_menu.set_visible(true);
                        file.imp().directory_menu.set_visible(false);
                    }
                }

                self.imp().files.append(&file);
            }
        }
    }

    pub fn get_music_directory(&self) {
        let state = self.imp().state.upgrade();

        let rt = tokio::runtime::Runtime::new().unwrap();
        let response_ = rt.block_on(async {
            let url = build_url();
            let mut client = SettingsServiceClient::connect(url).await?;
            let response = client
                .get_global_settings(GetGlobalSettingsRequest {})
                .await?
                .into_inner();
            Ok::<GetGlobalSettingsResponse, Error>(response)
        });

        if let Ok(response) = response_ {
            self.imp()
                .music_directory
                .replace(Some(response.music_dir.clone()));
            if let Some(state) = state {
                state.set_music_directory(&response.music_dir);
            }
        }
    }

    pub fn go_back(&self) {
        let music_directory = self.imp().music_directory.borrow();
        let music_directory = music_directory.clone().unwrap();
        let state = self.imp().state.upgrade().unwrap();
        let parent = match state.current_path() {
            Some(current_path) => match Path::new(&current_path).parent() {
                Some(parent) => parent.to_str().unwrap().to_string(),
                None => music_directory.clone(),
            },
            None => music_directory.clone(),
        };

        if parent == music_directory {
            let go_back_button = self.imp().go_back_button.borrow();
            let go_back_button = go_back_button.clone().unwrap();
            go_back_button.set_visible(false);
        }

        self.load_files(Some(parent));
    }
}

fn build_url() -> String {
    let host = env::var("ROCKBOX_HOST").unwrap_or("localhost".to_string());
    let port = env::var("ROCKBOX_PORT").unwrap_or("6061".to_string());
    format!("tcp://{}:{}", host, port)
}
