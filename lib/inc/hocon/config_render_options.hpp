#pragma once

namespace hocon {

    /**
     * <p>
     * A set of options related to rendering a {@link config_value}. Passed to
     * {@link config_value#render(config_render_options)}.
     *
     * <p>
     * Here is an example of creating a {@code config_render_options}:
     *
     * <pre>
     *     config_render_options options =
     *         config_render_options().set_comments(false)
     * </pre>
     */
    class config_render_options {
    public:
        /** Leaving the default arguments will result in a verbose rendering,
         * which contains comments and therefore is not valid JSON.
         * See {@link config_render_options#concise} for stripped-down options.
         */
        config_render_options(bool origin_comments = true, bool comments = true,
                              bool formatted = true, bool json = true);

        /**
         * Returns concise render options (no whitespace or comments). For a
         * resolved {@link config}, the concise rendering will be valid JSON.
         *
         * @return the concise render options
         */
        static config_render_options concise();

        /**
         * Returns options with comments toggled. This controls human-written
         * comments but not the autogenerated "origin of this setting" comments,
         * which are controlled by {@link config_render_options#set_origin_comments}.
         *
         * @param value
         *            true to include comments in the render
         * @return options with requested setting for comments
         */
        config_render_options set_comments(bool value);

        /**
         * Returns whether the options enable comments. This method is mostly used
         * by the config lib internally, not by applications.
         *
         * @return true if comments should be rendered
         */
        bool get_comments() const;

        /**
         * Returns options with origin comments toggled. If this is enabled, the
         * library generates comments for each setting based on the
         * {@link config_value#origin} of that setting's value. For example these
         * comments might tell you which file a setting comes from.
         *
         * <p>
         * {@code set_origin_comments()} controls only these autogenerated
         * "origin of this setting" comments, to toggle regular comments use
         * {@link config_render_options#set_comments}.
         *
         * @param value
         *            true to include autogenerated setting-origin comments in the
         *            render
         * @return options with origin comments toggled
         */
        config_render_options set_origin_comments(bool value);

        /**
         * Returns whether the options enable automated origin comments. This method
         * is mostly used by the config lib internally, not by applications.
         *
         * @return true if origin comments should be rendered
         */
        bool get_origin_comments() const;

        /**
         * Returns options with formatting toggled. Formatting means indentation and
         * whitespace, enabling formatting makes things prettier but larger.
         *
         * @param value
         *            true to enable formatting
         * @return options with requested setting for formatting
         */
        config_render_options set_formatted(bool value);

        /**
         * Returns whether the options enable formatting. This method is mostly used
         * by the config lib internally, not by applications.
         *
         * @return true if the options enable formatting
         */
        bool get_formatted() const;

        /**
         * Returns options with JSON toggled. JSON means that HOCON extensions
         * (omitting commas, quotes for example) won't be used. However, whether to
         * use comments is controlled by the separate {@link #set_comments(boolean)}
         * and {@link #set_origin_comments(boolean)} options. So if you enable
         * comments you will get invalid JSON despite setting this to true.
         *
         * @param value
         *            true to include non-JSON extensions in the render
         * @return options with requested setting for JSON
         */
        config_render_options set_json(bool value);

        /**
         * Returns whether the options enable JSON. This method is mostly used by
         * the config lib internally, not by applications.
         *
         * @return true if only JSON should be rendered
         */
        bool get_json() const;

    private:
        bool _origin_comments;
        bool _comments;
        bool _formatted;
        bool _json;
    };

}  // namespace hocon
