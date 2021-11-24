﻿using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

/*

 To configure projects like these for the first time, you need to run 

    DllExport.bat -action Configure

 Then set the right namespace (ZIPReader in this case). The project needs to be 
 reloaded and recompiled.

 */

namespace ZIPReader
{
    public class Main
    {
        static string m_sZIPFileName = null;
        static string m_sTempPath = null;
        // Enable verbosity
        static bool m_Verbose = false;

        [DllExport(CallingConvention.Cdecl)]
        public static void SetZIPVerbosity(bool Verbose)
        {
            m_Verbose = Verbose;
        }

        static protected string GetTempDirName(string sZIPFileName)
        {
            if (sZIPFileName == null)
            {
                Trace.WriteLineIf(m_Verbose, "[DBG] [C#] Cannot generate temp dir name, sZIPFileName is null");
                return "";
            }

            int idx = -1;

            idx = sZIPFileName.LastIndexOf('.');
            if (idx < 0)
            {
                Trace.WriteLineIf(m_Verbose, "[DBG] [C#] Cannot generate temp dir name, no extension in [" + sZIPFileName + "]");
                return "";
            }

            // Generate the temp dir for this .ZIP file
            // For instance, if the file is Effects\TieFighterCockpit.zip, then we'll create
            // Effects\TieFighterCockpit_tmp_zip to unzip the contents of the file.
            return sZIPFileName.Substring(0, idx) + "_tmp_zip";
        }

        static protected bool FileAlreadyUnzipped(string sZIPFileName)
        {
            string sTempPath = GetTempDirName(sZIPFileName);
            if (sTempPath == "")
                return false;
            if (Directory.Exists(sTempPath)) {
                m_sZIPFileName = sZIPFileName;
                m_sTempPath = sTempPath;
                return true;
            }
            return false;
        }

        static protected bool CreateTempDir(string sZIPFileName, out string sTempPath)
        {
            sTempPath = GetTempDirName(sZIPFileName);
            if (sTempPath == "")
            {
                Trace.WriteIf(m_Verbose, "[DBG] [C#] Could not create temp ZIP directory");
                return false;
            }

            // If the temp directory already exists, we assume this ZIP file has already been
            // unzipped
            if (Directory.Exists(sTempPath))
            {
                Trace.WriteIf(m_Verbose, "[DBG] [C#] Temp ZIP directory [" + sTempPath + "] already exists");
                return true;
            }
            // The temp directory does not exist, create it.
            Directory.CreateDirectory(sTempPath);
            Trace.WriteIf(m_Verbose, "[DBG] [C#] Temp ZIP directory [" + sTempPath + "] created");
            return true;
        }

        static string FindUnzippedImage(int GroupId, int ImageId)
        {
            string sPath = null;
            if (!FileAlreadyUnzipped(m_sZIPFileName))
            {
                Trace.WriteLine("[DBG] [C#] Load a ZIP file first");
                return null;
            }

            string[] ImageExtensions = { ".png", ".jpg", ".gif" };
            sPath = Path.Combine(Path.Combine(m_sTempPath, GroupId.ToString()), ImageId.ToString());
            //Trace.WriteLineIf(m_Verbose, "[DBG] [C#] Searching for image " + GroupId + "-" + ImageId + " in [" + sPath + "]");
            foreach (var Extension in ImageExtensions) {
                string sFullPath = sPath + Extension;
                if (File.Exists(sFullPath))
                {
                    //Trace.WriteLineIf(m_Verbose, "[DBG] [C#] Found image: [" + sFullPath + "] for " + GroupId + "-" + ImageId);
                    return sFullPath;
                }
            }

            return null;
        }

        [DllExport(CallingConvention.Cdecl)]
        public static bool LoadZIPFile([MarshalAs(UnmanagedType.LPStr)] string sZIPFileName)
        {
            ZipArchive ZIPFile = null;
            bool result = false;

            // First, let's check if this ZIP file is already loaded:
            if (FileAlreadyUnzipped(sZIPFileName))
            {
                Trace.WriteLineIf(m_Verbose, "[DBG] [C#] ZIP File " + sZIPFileName + " already loaded");
                return true;
            }

            // Now let's create the temporary directory to unzip the file to
            if (CreateTempDir(sZIPFileName, out m_sTempPath))
                Trace.WriteLineIf(m_Verbose, "[DBG] [C#] ZIP Temp path: " + m_sTempPath);
            else
                return false;
            
            try
            {
                Trace.WriteLineIf(m_Verbose, "[DBG] [C#] Loading File: " + sZIPFileName);
                ZIPFile = ZipFile.OpenRead(sZIPFileName);
                if (ZIPFile == null)
                {
                    Trace.WriteLineIf(m_Verbose, "[DBG] [C#] Failed when loading: " + sZIPFileName);
                    return false;
                }

                // Unzip the file into the temporary directory
                Trace.WriteLine("[DBG] [C#] Unzipping [" + sZIPFileName + "]");
                ZIPFile.ExtractToDirectory(m_sTempPath);
                Trace.WriteLine("[DBG] [C#] Finished unzipping file");
                // "Cache" the name of the ZIP file and the temp path for future reference
                m_sZIPFileName = sZIPFileName;
                result = true;
            }
            catch (Exception e)
            {
                Trace.WriteLine("[DBG] [C#] Exception " + e + " when opening " + sZIPFileName);
                result = false;
            }
            finally
            {
                if (ZIPFile != null)
                {
                    Trace.WriteLineIf(m_Verbose, "[DBG] [C#] Closing [" + sZIPFileName + "]");
                    ZIPFile.Dispose();
                }
                ZIPFile = null;
            }
            
            return result;
        }

        [DllExport(CallingConvention.Cdecl)]
        public static unsafe bool GetZIPImageMetadata(int GroupId, int ImageId, short *Width_out, short *Height_out, byte *ImagePath_out, int ImagePathSize)
        {
            string sImagePath = FindUnzippedImage(GroupId, ImageId);
            if (sImagePath == null)
                return false;

            if (ImagePath_out != null)
            {
                int j = 0;
                for (int i = 0; i < sImagePath.Length; i++)
                {
                    if (j < ImagePathSize - 1)
                        ImagePath_out[j++] = (byte)sImagePath[i];
                }
                ImagePath_out[j] = 0;
            }

            using (var fileStream = new FileStream(sImagePath, FileMode.Open, FileAccess.Read, FileShare.Read))
            {
                using (var image = Image.FromStream(fileStream, false, false))
                {
                    if (Height_out != null) *Height_out = (short)image.Height;
                    if (Width_out != null) *Width_out = (short)image.Width;
                }
            }

            return true;
        }

        [DllExport(CallingConvention.Cdecl)]
        public static unsafe bool ReadZIPImageData(byte *RawData_out, int RawData_size)
        {
            /*
            if (m_DATImage == null) {
                Trace.WriteLine("[DBG] [C#] Must cache an image first");
                return false;
            }
            
            //m_DATImage.ConvertToFormat25(); // Looks like there's no need to do any conversion
            short W = m_DATImage.Width;
            short H = m_DATImage.Height;
            byte[] data = m_DATImage.GetImageData();
            int len = data.Length;
            if (m_Verbose)
                Trace.WriteLine("[DBG] [C#] RawData, W*H*4 = " + (W * H * 4) + ", len: " + len + ", Format: " + m_DATImage.Format);

            if (RawData_out == null)
            {
                Trace.WriteLine("[DBG] [C#] ReadZIPImageData: output buffer should not be NULL");
                return false;
            }

            try
            {
                int min_len = RawData_size;
                if (data.Length < min_len) min_len = data.Length;
                // For some reason, the images are still upside down when used as SRVs
                // So, let's flip them here. RowOfs and RowStride are used to flip the
                // image by reading it "backwards".
                UInt32 OfsOut = 0, OfsIn = 0, RowStride = (UInt32 )W * 4, RowOfs = (UInt32)(H - 1) * RowStride;
                for (int y = 0; y < H; y++)
                {
                    OfsIn = RowOfs; // Flip the image
                    for (int x = 0; x < W; x++)
                    {
                        RawData_out[OfsOut + 2] = data[OfsIn + 0]; // B
                        RawData_out[OfsOut + 1] = data[OfsIn + 1]; // G
                        RawData_out[OfsOut + 0] = data[OfsIn + 2]; // R
                        RawData_out[OfsOut + 3] = data[OfsIn + 3]; // A
                        OfsIn += 4;
                        OfsOut += 4;
                    }
                    // Flip the image and prevent underflows:
                    RowOfs -= RowStride;
                    if (RowOfs < 0) RowOfs = 0;
                }
            }
            catch (Exception e)
            {
                Trace.WriteLine("[DBG] [C#] Exception: " + e + ", caught in ReadZIPImageData");
                return false;
            }
            */
            return true;
        }

        [DllExport(CallingConvention.Cdecl)]
        public static int GetZIPGroupImageCount(int GroupId)
        {
            if (m_sTempPath == null)
            {
                Trace.WriteLine("[DBG] [C#] Load a ZIP file first");
                return 0;
            }

            return Directory.EnumerateFiles(m_sTempPath + "\\" + GroupId).Count();
        }

        protected static int GetImageIdFromImageName(string sImageName)
        {
            int result = -1;
            int dot_idx = sImageName.LastIndexOf('.');
            int slash_idx = sImageName.LastIndexOf('\\');
            string sRoot = sImageName.Substring(slash_idx + 1, dot_idx - (slash_idx + 1));
            Trace.WriteLineIf(m_Verbose, "[DBG] [C#] sRoot: [" + sRoot + "]");
            if (int.TryParse(sRoot, out result))
                return result;
            else
                return -1;
        }

        [DllExport(CallingConvention.Cdecl)]
        public static unsafe bool GetZIPGroupImageList(int GroupId, short *ImageIds_out, int ImageIds_size)
        {
            if (m_sTempPath == null)
            {
                Trace.WriteLine("[DBG] [C#] Load a ZIP file first");
                return false;
            }

            string sPath = m_sTempPath + "\\" + GroupId + "\\";
            var sImageNames = Directory.EnumerateFiles(sPath);
            ArrayList ImageIds = new ArrayList();
            foreach (string sImageName in sImageNames)
            {
                ImageIds.Add(GetImageIdFromImageName(sImageName));
            }
            ImageIds.Sort();

            int Ofs = 0;
            foreach (int ImageId in ImageIds)
            {
                ImageIds_out[Ofs] = (short)ImageId;
                Trace.WriteLineIf(m_Verbose, "[DBG] [C#] Stored ImageId: " + ImageIds_out[Ofs]);
                // Advance the output index, but prevent an overflow
                if (Ofs < ImageIds_size) Ofs++;
            }
            return true;
        }

        /*
         * Removes all the temp directories created when unzipping files
         */
        [DllExport(CallingConvention.Cdecl)]
        public static void DeleteAllTempZIPDirectories()
        {
            Trace.WriteLine("[DBG] [C#] Deleting all temporary directories...");
            var sDirectories = Directory.EnumerateDirectories(".\\Effects\\");
            foreach (string sDirectory in sDirectories)
            {
                if (sDirectory.EndsWith("_tmp_zip"))
                {
                    Trace.WriteLine("[DBG] [C#] Deleting temporary directory: " + sDirectory);
                    Directory.Delete(sDirectory, true);
                }
            }
        }

    }
}
