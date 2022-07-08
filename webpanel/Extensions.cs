﻿using Nancy;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace SyberiaWebPanel
{
    public static class Extensions
    {
        public static Response NoCache(this Response response)
        {
            response.Headers.Add("Cache-Control", "no-cache, no-store, must-revalidate");
            response.Headers.Add("Pragma", "no-cache");
            response.Headers.Add("Expires", "0");
            return response;
        }

        public static void  Replace(this StringBuilder builder, string from, int to)
        {
            builder.Replace(from, to.ToString());
        }

        public static void Replace(this StringBuilder builder, string from, bool to)
        {
            builder.Replace(from, to ? "checked" : "");
        }

        public static void Replace(this StringBuilder builder, string from, float to)
        {
            builder.Replace(from, to.ToString().Replace(',', '.'));
        }
    }
}
