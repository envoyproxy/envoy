require "builder"

set :layout, :article

activate :livereload
activate :i18n, :langs => [:en], :lang_map => { :en => :english }
activate :directory_indexes
activate :autoprefixer

activate :deploy do |deploy|
  deploy.deploy_method = :git
end

set :markdown, :tables => true, :autolink => true, :gh_blockcode => true, :fenced_code_blocks => true, :with_toc_data => true
set :markdown_engine, :redcarpet

configure :development do
  set :debug_assets, true
end

configure :build do
  activate :minify_css
  activate :minify_javascript
  activate :relative_assets
  activate :asset_hash
  set :relative_links, true
end

helpers do
  def active_link_to(caption, url, options = {})
    if current_page.url == "#{url}/"
      options[:class] = "doc-item-active"
    end

    link_to(caption, url, options)
  end
end

page "/localizable/community/built_using_middleman", layout: :example
